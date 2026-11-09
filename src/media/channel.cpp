/**
 * @file    channel.cpp
 * @brief   一路解码。包队列满了丢到下一个关键帧，订阅槽只留最新的一帧。
 */
#include "media/pm_channel.h"

#include "packet_queue.h"

#include "plt/plt_jpeg.h"
#include "plt/plt_mem.h"
#include "plt/plt_rga.h"
#include "plt/plt_vdec.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace
{

const int kSubs = 8;
const int kJpegQuality = 80;
const int kBoxThickness = 2;
const size_t kMaxPacket = 8u * 1024u * 1024u;

struct Held
{
    int used;
    int borrowed;
    PmImage image;
    void *owner;
    int64_t pts_us;
    int64_t recv_us;
    uint64_t seq;
};

struct Sub
{
    int used;
    int generation;
    int calling;
    int64_t interval_us;
    int64_t next_us;
    PmFrameSink sink;
    void *user;
    Held slot;
    Held held;
};

bool IsAnnexb(const uint8_t *data, size_t size)
{
    return size >= 4 && data[0] == 0 && data[1] == 0 &&
           (data[2] == 1 || (data[2] == 0 && data[3] == 1));
}

bool ReadNal(const uint8_t *extra, size_t size, size_t *pos, std::vector<uint8_t> *out)
{
    if (*pos + 2 > size)
    {
        return false;
    }
    const int len = (extra[*pos] << 8) | extra[*pos + 1];
    *pos += 2;
    if (len <= 0 || *pos + (size_t)len > size)
    {
        return false;
    }
    out->push_back(0);
    out->push_back(0);
    out->push_back(0);
    out->push_back(1);
    out->insert(out->end(), extra + *pos, extra + *pos + len);
    *pos += (size_t)len;
    return true;
}

/* H.264 的 AVCC 参数集转成 Annex B。已经是起始码，或认不出格式，就原样留下。 */
std::vector<uint8_t> ToAnnexb(const uint8_t *extra, size_t size)
{
    std::vector<uint8_t> raw(extra, extra + size);
    if (IsAnnexb(extra, size) || size < 7 || extra[0] != 1)
    {
        return raw;
    }
    std::vector<uint8_t> out;
    size_t pos = 5;
    const int sps_count = extra[pos] & 0x1f;
    pos++;
    for (int i = 0; i < sps_count; ++i)
    {
        if (!ReadNal(extra, size, &pos, &out))
        {
            return raw;
        }
    }
    if (pos >= size)
    {
        return out.empty() ? raw : out;
    }
    const int pps_count = extra[pos];
    pos++;
    for (int i = 0; i < pps_count; ++i)
    {
        if (!ReadNal(extra, size, &pos, &out))
        {
            return raw;
        }
    }
    return out.empty() ? raw : out;
}

void ClearHeld(Held *held)
{
    memset(held, 0, sizeof(*held));
    held->image.fd = -1;
}

int Due(const Sub *sub, int64_t recv_us)
{
    if (sub->interval_us <= 0)
    {
        return 1;
    }
    return recv_us >= sub->next_us;
}

int MakeId(const Sub *sub, int index)
{
    return (sub->generation << 3) | index;
}

}

struct PmChannel
{
    PmPacketQueue queue;
    PltVdec *dec;
    int index;
    pthread_t thread;
    int thread_started;
    std::atomic<int> stop;
    std::atomic<int> unsupported;
    std::mutex sub_mutex;
    std::condition_variable sub_cond;
    Sub subs[kSubs];
    uint64_t seq;
    std::vector<uint8_t> extra;
    int extra_pending;
    std::deque<std::pair<int64_t, int64_t> > notes;
    int64_t last_recv;

    explicit PmChannel(int limit)
        : queue(limit)
        , dec(NULL)
        , index(0)
        , thread_started(0)
        , stop(0)
        , unsupported(0)
        , seq(0)
        , extra_pending(0)
        , last_recv(0)
    {
        memset(&thread, 0, sizeof(thread));
        for (int i = 0; i < kSubs; ++i)
        {
            memset(&subs[i], 0, sizeof(subs[i]));
            subs[i].generation = 1;
            subs[i].slot.image.fd = -1;
            subs[i].held.image.fd = -1;
        }
    }

    Sub *find(int id)
    {
        if (id < 0)
        {
            return NULL;
        }
        const int index = id & (kSubs - 1);
        const int generation = id >> 3;
        Sub *sub = &subs[index];
        if (!sub->used || sub->generation != generation)
        {
            return NULL;
        }
        return sub;
    }

    void release_held(Held *held)
    {
        if (!held->used)
        {
            return;
        }
        if (held->borrowed)
        {
            PmDecodedFrame frame;
            memset(&frame, 0, sizeof(frame));
            frame.image = held->image;
            frame.owner = held->owner;
            plt_vdec_release(dec, &frame);
        }
        else
        {
            plt_mem_free(&held->image);
        }
        ClearHeld(held);
    }

    void remember(int64_t pts, int64_t recv)
    {
        last_recv = recv;
        notes.push_back(std::make_pair(pts, recv));
        if (notes.size() > 64)
        {
            notes.pop_front();
        }
    }

    int64_t lookup(int64_t pts)
    {
        for (std::deque<std::pair<int64_t, int64_t> >::iterator it = notes.begin(); it != notes.end(); ++it)
        {
            if (it->first == pts)
            {
                const int64_t recv = it->second;
                notes.erase(it);
                return recv;
            }
        }
        return last_recv;
    }

    int drain()
    {
        int got = 0;
        for (;;)
        {
            PmDecodedFrame frame;
            const PmStatus status = plt_vdec_get(dec, &frame);
            if (status == PM_AGAIN)
            {
                return got;
            }
            if (status == PM_UNSUPPORTED)
            {
                unsupported.store(1);
                return got;
            }
            if (status != PM_OK)
            {
                return got;
            }
            if (!pm_image_valid(&frame.image))
            {
                plt_vdec_release(dec, &frame);
                continue;
            }
            deliver(&frame, lookup(frame.pts_us));
            got++;
            if (unsupported.load())
            {
                return got;
            }
        }
    }

    PmStatus send_one(const uint8_t *data, int len, int64_t pts, int64_t recv)
    {
        remember(pts, recv);
        for (int spin = 0; spin < 3; ++spin)
        {
            const PmStatus status = plt_vdec_send(dec, data, len, pts, 0);
            if (status == PM_OK)
            {
                return PM_OK;
            }
            if (status == PM_UNSUPPORTED)
            {
                unsupported.store(1);
                return status;
            }
            if (status != PM_AGAIN)
            {
                return status;
            }
            if (drain() == 0)
            {
                break;
            }
        }
        return PM_AGAIN;
    }

    void deliver(PmDecodedFrame *frame, int64_t recv_us)
    {
        struct Job
        {
            int index;
            int original;
        };
        Job jobs[kSubs];
        int job_count = 0;
        {
            std::lock_guard<std::mutex> lock(sub_mutex);
            for (int i = 0; i < kSubs; ++i)
            {
                if (!subs[i].used || !Due(&subs[i], recv_us))
                {
                    continue;
                }
                jobs[job_count].index = i;
                jobs[job_count].original = job_count == 0 ? 1 : 0;
                job_count++;
            }
        }
        if (job_count == 0)
        {
            plt_vdec_release(dec, frame);
            return;
        }

        Held made[kSubs];
        int made_ok[kSubs];
        memset(made_ok, 0, sizeof(made_ok));
        for (int i = 0; i < job_count; ++i)
        {
            ClearHeld(&made[i]);
            if (jobs[i].original)
            {
                made[i].used = 1;
                made[i].borrowed = 1;
                made[i].image = frame->image;
                made[i].owner = frame->owner;
                made[i].pts_us = frame->pts_us;
                made[i].recv_us = recv_us;
                made_ok[i] = 1;
                continue;
            }
            PmImage copy;
            memset(&copy, 0, sizeof(copy));
            copy.fd = -1;
            if (plt_mem_alloc(frame->image.width, frame->image.height, PM_PIX_NV12, &copy) != PM_OK)
            {
                continue;
            }
            copy.range = frame->image.range;
            if (plt_rga_blit(&frame->image, NULL, &copy, NULL) != PM_OK)
            {
                plt_mem_free(&copy);
                continue;
            }
            made[i].used = 1;
            made[i].borrowed = 0;
            made[i].image = copy;
            made[i].pts_us = frame->pts_us;
            made[i].recv_us = recv_us;
            made_ok[i] = 1;
        }

        struct Callback
        {
            PmFrameSink sink;
            void *user;
            PmVideoFrame frame;
            int index;
        };
        Callback callbacks[kSubs];
        int callback_count = 0;
        int original_kept = 0;
        {
            std::lock_guard<std::mutex> lock(sub_mutex);
            seq++;
            for (int i = 0; i < job_count; ++i)
            {
                Sub *sub = &subs[jobs[i].index];
                if (!made_ok[i] || !sub->used)
                {
                    continue;
                }
                if (sub->slot.used)
                {
                    release_held(&sub->slot);
                }
                made[i].seq = seq;
                sub->slot = made[i];
                made_ok[i] = 0;
                if (jobs[i].original)
                {
                    original_kept = 1;
                }
                if (sub->interval_us > 0)
                {
                    sub->next_us = recv_us + sub->interval_us;
                }
                if (sub->sink != NULL)
                {
                    Callback *callback = &callbacks[callback_count];
                    memset(callback, 0, sizeof(*callback));
                    callback->sink = sub->sink;
                    callback->user = sub->user;
                    callback->index = jobs[i].index;
                    callback->frame.channel = index;
                    callback->frame.seq = sub->slot.seq;
                    callback->frame.pts_us = sub->slot.pts_us;
                    callback->frame.recv_us = sub->slot.recv_us;
                    callback->frame.image = sub->slot.image;
                    sub->calling++;
                    callback_count++;
                }
            }
        }
        for (int i = 0; i < job_count; ++i)
        {
            if (made_ok[i] && !jobs[i].original)
            {
                plt_mem_free(&made[i].image);
            }
        }
        if (!original_kept)
        {
            plt_vdec_release(dec, frame);
        }

        for (int i = 0; i < callback_count; ++i)
        {
            callbacks[i].sink(&callbacks[i].frame, callbacks[i].user);
        }
        if (callback_count > 0)
        {
            std::lock_guard<std::mutex> lock(sub_mutex);
            for (int i = 0; i < callback_count; ++i)
            {
                subs[callbacks[i].index].calling--;
            }
            sub_cond.notify_all();
        }
    }

    void run()
    {
        char name[16];
        int number = index;
        if (number < 0 || number > 99)
        {
            number = 0;
        }
        snprintf(name, sizeof(name), "media.dec.%d", number);
        pthread_setname_np(pthread_self(), name);

        while (!stop.load())
        {
            PmQueuedPacket packet;
            if (!queue.pop(&packet))
            {
                break;
            }
            if (stop.load() || unsupported.load())
            {
                break;
            }
            if (extra_pending)
            {
                const PmStatus extra_status = send_one(extra.data(),
                                                       (int)extra.size(),
                                                       packet.pts_us,
                                                       packet.recv_us);
                if (extra_status != PM_OK)
                {
                    queue.drop_until_key();
                    if (unsupported.load())
                    {
                        break;
                    }
                    continue;
                }
                extra_pending = 0;
            }
            const PmStatus status = send_one(packet.data.data(),
                                             (int)packet.data.size(),
                                             packet.pts_us,
                                             packet.recv_us);
            if (status == PM_AGAIN || status == PM_ERR)
            {
                queue.drop_until_key();
            }
            drain();
            if (unsupported.load())
            {
                break;
            }
        }
    }
};

namespace
{

void *ThreadMain(void *arg)
{
    PmChannel *channel = (PmChannel *)arg;
    try
    {
        channel->run();
    }
    catch (const std::bad_alloc &)
    {
        channel->stop.store(1);
    }
    return NULL;
}

}

extern "C" PmStatus pm_channel_open(const PmChannelConfig *cfg, PmChannel **out)
{
    if (out != NULL)
    {
        *out = NULL;
    }
    if (cfg == NULL || out == NULL || cfg->buf_count <= 0 || cfg->queue_limit <= 0)
    {
        return PM_ERR;
    }
    if (cfg->codec != PM_CODEC_H264 && cfg->codec != PM_CODEC_H265)
    {
        return PM_ERR;
    }

    PmChannel *channel = NULL;
    try
    {
        channel = new PmChannel(cfg->queue_limit);
    }
    catch (const std::bad_alloc &)
    {
        return PM_ERR;
    }
    channel->index = cfg->channel;
    if (cfg->extradata != NULL && cfg->extradata_size > 0)
    {
        try
        {
            channel->extra = ToAnnexb(cfg->extradata, cfg->extradata_size);
        }
        catch (const std::bad_alloc &)
        {
            delete channel;
            return PM_ERR;
        }
        channel->extra_pending = channel->extra.empty() ? 0 : 1;
    }
    if (plt_vdec_open(cfg->codec, cfg->buf_count, &channel->dec) != PM_OK)
    {
        delete channel;
        return PM_ERR;
    }
    if (pthread_create(&channel->thread, NULL, ThreadMain, channel) != 0)
    {
        plt_vdec_close(channel->dec);
        delete channel;
        return PM_ERR;
    }
    channel->thread_started = 1;
    *out = channel;
    return PM_OK;
}

extern "C" void pm_channel_close(PmChannel *channel)
{
    if (channel == NULL)
    {
        return;
    }
    channel->stop.store(1);
    channel->queue.stop();
    if (channel->thread_started)
    {
        pthread_join(channel->thread, NULL);
        channel->thread_started = 0;
    }
    for (int i = 0; i < kSubs; ++i)
    {
        channel->release_held(&channel->subs[i].slot);
        channel->release_held(&channel->subs[i].held);
    }
    if (channel->dec != NULL)
    {
        plt_vdec_close(channel->dec);
        channel->dec = NULL;
    }
    delete channel;
}

extern "C" PmStatus pm_channel_push(PmChannel *channel, const PmPacketView *packet)
{
    if (channel == NULL || packet == NULL || packet->data == NULL || packet->size == 0 || packet->size > kMaxPacket)
    {
        return PM_ERR;
    }
    if (channel->unsupported.load())
    {
        return PM_UNSUPPORTED;
    }
    if (channel->stop.load())
    {
        return PM_ERR;
    }
    try
    {
        if (!channel->queue.push(packet))
        {
            return PM_ERR;
        }
    }
    catch (const std::bad_alloc &)
    {
        return PM_ERR;
    }
    return PM_OK;
}

extern "C" int pm_channel_dropped(const PmChannel *channel)
{
    if (channel == NULL)
    {
        return -1;
    }
    return channel->queue.dropped();
}

extern "C" int pm_channel_subscribe(PmChannel *channel, double fps, PmFrameSink sink, void *user)
{
    if (channel == NULL || fps < 0)
    {
        return PM_ERR;
    }
    std::lock_guard<std::mutex> lock(channel->sub_mutex);
    for (int i = 0; i < kSubs; ++i)
    {
        Sub *sub = &channel->subs[i];
        if (sub->used)
        {
            continue;
        }
        sub->used = 1;
        sub->calling = 0;
        sub->interval_us = 0;
        if (fps > 0)
        {
            sub->interval_us = (int64_t)(1000000.0 / fps);
            if (sub->interval_us < 1)
            {
                sub->interval_us = 1;
            }
        }
        sub->next_us = 0;
        sub->sink = sink;
        sub->user = user;
        ClearHeld(&sub->slot);
        ClearHeld(&sub->held);
        return MakeId(sub, i);
    }
    return PM_ERR;
}

extern "C" PmStatus pm_channel_unsubscribe(PmChannel *channel, int sub_id)
{
    if (channel == NULL)
    {
        return PM_ERR;
    }
    std::unique_lock<std::mutex> lock(channel->sub_mutex);
    Sub *sub = channel->find(sub_id);
    if (sub == NULL)
    {
        return PM_ERR;
    }
    while (sub->calling > 0)
    {
        channel->sub_cond.wait(lock);
    }
    channel->release_held(&sub->slot);
    channel->release_held(&sub->held);
    sub->used = 0;
    sub->sink = NULL;
    sub->user = NULL;
    sub->generation++;
    if (sub->generation > (1 << 28))
    {
        sub->generation = 1;
    }
    return PM_OK;
}

extern "C" PmStatus pm_channel_acquire(PmChannel *channel, int sub_id, PmVideoFrame *frame)
{
    if (channel == NULL || frame == NULL)
    {
        return PM_ERR;
    }
    memset(frame, 0, sizeof(*frame));
    frame->image.fd = -1;
    std::lock_guard<std::mutex> lock(channel->sub_mutex);
    Sub *sub = channel->find(sub_id);
    if (sub == NULL)
    {
        return PM_ERR;
    }
    if (sub->held.used)
    {
        return PM_ERR;
    }
    if (!sub->slot.used)
    {
        return PM_AGAIN;
    }
    sub->held = sub->slot;
    ClearHeld(&sub->slot);
    frame->channel = channel->index;
    frame->seq = sub->held.seq;
    frame->pts_us = sub->held.pts_us;
    frame->recv_us = sub->held.recv_us;
    frame->image = sub->held.image;
    return PM_OK;
}

extern "C" PmStatus pm_channel_release(PmChannel *channel, int sub_id)
{
    if (channel == NULL)
    {
        return PM_ERR;
    }
    std::lock_guard<std::mutex> lock(channel->sub_mutex);
    Sub *sub = channel->find(sub_id);
    if (sub == NULL)
    {
        return PM_ERR;
    }
    channel->release_held(&sub->held);
    return PM_OK;
}

extern "C" PmStatus pm_channel_snapshot(PmChannel *channel,
                                       const PmVideoFrame *frame,
                                       const PmRect *boxes,
                                       int box_count,
                                       uint8_t **jpeg,
                                       size_t *jpeg_len)
{
    if (jpeg != NULL)
    {
        *jpeg = NULL;
    }
    if (jpeg_len != NULL)
    {
        *jpeg_len = 0;
    }
    if (channel == NULL || !pm_image_valid(frame == NULL ? NULL : &frame->image) || jpeg == NULL || jpeg_len == NULL)
    {
        return PM_ERR;
    }
    if (frame->image.format != PM_PIX_NV12 || box_count < 0 || (box_count > 0 && boxes == NULL))
    {
        return PM_ERR;
    }

    PmImage copy;
    memset(&copy, 0, sizeof(copy));
    copy.fd = -1;
    if (plt_mem_alloc(frame->image.width, frame->image.height, PM_PIX_NV12, &copy) != PM_OK)
    {
        return PM_ERR;
    }
    copy.range = frame->image.range;
    if (plt_rga_blit(&frame->image, NULL, &copy, NULL) != PM_OK)
    {
        plt_mem_free(&copy);
        return PM_ERR;
    }
    if (box_count > 0 && plt_rga_boxes(&copy, boxes, box_count, 0x0000ff00, kBoxThickness) != PM_OK)
    {
        plt_mem_free(&copy);
        return PM_ERR;
    }
    const PmStatus status = plt_jpeg_encode(&copy, kJpegQuality, jpeg, jpeg_len);
    plt_mem_free(&copy);
    return status;
}
