/**
 * @file    net_pull.c
 * @brief   FFmpeg 只负责解封装。一帧 Annex B 交给调用方，半帧在断流时丢掉。
 */
#include "net_pull.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
#include <libavutil/time.h>

struct NetPull
{
    AVFormatContext *fmt;
    AVBSFContext *bsf;
    AVCodecParserContext *parser;
    AVCodecContext *avctx;
    AVPacket *packet;
    int video_index;
    int codec;
    int width;
    int height;
    uint8_t *extra;
    int extra_size;
    AVRational time_base;
    AVRational frame_rate;
    int64_t synth;
    int saw_key;
    int frame_key;
    int64_t frame_pts;
    uint8_t *frame;
    int frame_size;
    int frame_cap;
    uint8_t *tail;
    int tail_size;
    int64_t tail_pts;
    int dead;
    atomic_int stop;
    pthread_mutex_t lock;
};

static pthread_once_t g_network_once = PTHREAD_ONCE_INIT;

static void InitNetwork(void)
{
    /* 连接失败时 FFmpeg 会自己打到 stderr。这一层只用返回值。 */
    av_log_set_level(AV_LOG_QUIET);
    avformat_network_init();
}

static int Interrupt(void *opaque)
{
    NetPull *pull = opaque;
    return atomic_load(&pull->stop) != 0;
}

static int IsAnnexb(const uint8_t *data, int size)
{
    return size >= 4 && data[0] == 0 && data[1] == 0 &&
           (data[2] == 1 || (data[2] == 0 && data[3] == 1));
}

static int IsAvcc(const uint8_t *data, int size)
{
    return size >= 7 && data[0] == 1 && !IsAnnexb(data, size);
}

static int IsKey(const AVCodecParserContext *parser)
{
    if (parser->key_frame == 1)
    {
        return 1;
    }
    if (parser->key_frame == 0)
    {
        return 0;
    }
    return parser->pict_type == AV_PICTURE_TYPE_I;
}

static void Release(NetPull *pull)
{
    if (pull->fmt != NULL)
    {
        avformat_close_input(&pull->fmt);
    }
    if (pull->bsf != NULL)
    {
        av_bsf_free(&pull->bsf);
    }
    if (pull->parser != NULL)
    {
        av_parser_close(pull->parser);
        pull->parser = NULL;
    }
    if (pull->avctx != NULL)
    {
        avcodec_free_context(&pull->avctx);
    }
    av_packet_free(&pull->packet);
    free(pull->extra);
    free(pull->frame);
    free(pull->tail);
    pull->extra = NULL;
    pull->frame = NULL;
    pull->tail = NULL;
    pull->extra_size = 0;
    pull->frame_size = 0;
    pull->tail_size = 0;
}

static void Destroy(NetPull *pull)
{
    if (pull == NULL)
    {
        return;
    }
    Release(pull);
    pthread_mutex_destroy(&pull->lock);
    free(pull);
}

static int CopyExtra(NetPull *pull, const AVCodecParameters *par)
{
    if (par->extradata == NULL || par->extradata_size <= 0)
    {
        return 0;
    }
    pull->extra = malloc((size_t)par->extradata_size);
    if (pull->extra == NULL)
    {
        return -1;
    }
    memcpy(pull->extra, par->extradata, (size_t)par->extradata_size);
    pull->extra_size = par->extradata_size;
    return 0;
}

static BaseStatus SetupBsf(NetPull *pull, const AVStream *stream)
{
    const AVBitStreamFilter *filter;
    const char *name;
    int ret;
    if (!IsAvcc(pull->extra, pull->extra_size))
    {
        return BASE_OK;
    }
    name = pull->codec == BASE_CODEC_H265 ? "hevc_mp4toannexb" : "h264_mp4toannexb";
    filter = av_bsf_get_by_name(name);
    if (filter == NULL)
    {
        return BASE_ERR;
    }
    if (av_bsf_alloc(filter, &pull->bsf) < 0)
    {
        return BASE_ERR;
    }
    ret = avcodec_parameters_copy(pull->bsf->par_in, stream->codecpar);
    if (ret < 0)
    {
        return BASE_ERR;
    }
    pull->bsf->time_base_in = stream->time_base;
    if (av_bsf_init(pull->bsf) < 0)
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

static BaseStatus SetupParser(NetPull *pull, const AVCodecParameters *par)
{
    pull->parser = av_parser_init(par->codec_id);
    pull->avctx = avcodec_alloc_context3(NULL);
    if (pull->parser == NULL || pull->avctx == NULL)
    {
        return BASE_ERR;
    }
    if (avcodec_parameters_to_context(pull->avctx, par) < 0)
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

static BaseStatus Adopt(NetPull *pull, AVStream *stream)
{
    AVCodecParameters *par = stream->codecpar;
    if (par->codec_id == AV_CODEC_ID_H264)
    {
        pull->codec = BASE_CODEC_H264;
    }
    else if (par->codec_id == AV_CODEC_ID_HEVC)
    {
        pull->codec = BASE_CODEC_H265;
    }
    else
    {
        return BASE_UNSUPPORTED;
    }
    pull->width = par->width;
    pull->height = par->height;
    pull->time_base = stream->time_base;
    if (pull->time_base.num <= 0 || pull->time_base.den <= 0)
    {
        pull->time_base.num = 1;
        pull->time_base.den = 90000;
    }
    pull->frame_rate = stream->r_frame_rate;
    if (pull->frame_rate.num <= 0 || pull->frame_rate.den <= 0)
    {
        pull->frame_rate = stream->avg_frame_rate;
    }
    if (pull->frame_rate.num <= 0 || pull->frame_rate.den <= 0)
    {
        pull->frame_rate.num = 25;
        pull->frame_rate.den = 1;
    }
    if (CopyExtra(pull, par) != 0 || SetupBsf(pull, stream) != BASE_OK || SetupParser(pull, par) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (pull->bsf != NULL && pull->avctx->extradata != NULL)
    {
        av_freep(&pull->avctx->extradata);
        pull->avctx->extradata_size = 0;
    }
    return BASE_OK;
}

static int Keep(NetPull *pull, const uint8_t *data, int size)
{
    uint8_t *buf;
    if (size > pull->frame_cap)
    {
        buf = realloc(pull->frame, (size_t)size);
        if (buf == NULL)
        {
            return -1;
        }
        pull->frame = buf;
        pull->frame_cap = size;
    }
    memcpy(pull->frame, data, (size_t)size);
    pull->frame_size = size;
    return 0;
}

static int SaveTail(NetPull *pull, const uint8_t *data, int size, int64_t pts)
{
    int cap = size + AV_INPUT_BUFFER_PADDING_SIZE;
    uint8_t *buf = realloc(pull->tail, (size_t)cap);
    if (buf == NULL)
    {
        return -1;
    }
    memcpy(buf, data, (size_t)size);
    memset(buf + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    pull->tail = buf;
    pull->tail_size = size;
    pull->tail_pts = pts;
    return 0;
}

static int64_t NextStamp(NetPull *pull, int64_t pts, int64_t dts)
{
    int64_t ts = pts;
    AVRational step;
    if (ts == AV_NOPTS_VALUE)
    {
        ts = dts;
    }
    if (ts != AV_NOPTS_VALUE)
    {
        pull->synth++;
        return ts;
    }
    step.num = pull->frame_rate.den;
    step.den = pull->frame_rate.num;
    ts = av_rescale_q(pull->synth, step, pull->time_base);
    pull->synth++;
    return ts;
}

static int64_t ScaleUs(const NetPull *pull, int64_t ts)
{
    AVRational us;
    us.num = 1;
    us.den = 1000000;
    return av_rescale_q(ts, pull->time_base, us);
}

/* 返回 1 表示留下了一帧，0 表示还要更多数据，-1 表示失败。非关键帧在第一帧之前丢掉。 */
static int Parse(NetPull *pull, const uint8_t *data, int size, int64_t pts, int *used)
{
    uint8_t *out = NULL;
    int out_size = 0;
    int n = av_parser_parse2(pull->parser, pull->avctx, &out, &out_size, data, size, pts, pts, AV_NOPTS_VALUE);
    if (n < 0)
    {
        return -1;
    }
    *used = n;
    if (out_size <= 0)
    {
        return 0;
    }
    if (!pull->saw_key && !IsKey(pull->parser))
    {
        return 0;
    }
    if (Keep(pull, out, out_size) != 0)
    {
        return -1;
    }
    pull->saw_key = 1;
    pull->frame_key = IsKey(pull->parser);
    pull->frame_pts = pull->parser->pts != AV_NOPTS_VALUE ? pull->parser->pts : pts;
    return 1;
}

static BaseStatus Feed(NetPull *pull, const uint8_t *data, int size, int64_t pts, int *got)
{
    *got = 0;
    while (size > 0)
    {
        int used = 0;
        int produced = Parse(pull, data, size, pts, &used);
        if (produced < 0)
        {
            return BASE_ERR;
        }
        if (used <= 0)
        {
            break;
        }
        data += used;
        size -= used;
        if (produced == 1)
        {
            if (size > 0 && SaveTail(pull, data, size, pts) != 0)
            {
                return BASE_ERR;
            }
            *got = 1;
            return BASE_OK;
        }
    }
    return BASE_OK;
}

static BaseStatus NextVideo(NetPull *pull)
{
    int ret;
    if (pull->bsf != NULL)
    {
        ret = av_bsf_receive_packet(pull->bsf, pull->packet);
        if (ret == 0)
        {
            return BASE_OK;
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        {
            return BASE_ERR;
        }
    }
    for (;;)
    {
        if (atomic_load(&pull->stop) != 0)
        {
            return BASE_ERR;
        }
        ret = av_read_frame(pull->fmt, pull->packet);
        if (ret == AVERROR(EAGAIN))
        {
            if (atomic_load(&pull->stop) != 0)
            {
                return BASE_ERR;
            }
            av_usleep(10 * 1000);
            continue;
        }
        if (ret < 0)
        {
            return BASE_ERR;
        }
        if (pull->packet->stream_index != pull->video_index || pull->packet->size <= 0)
        {
            av_packet_unref(pull->packet);
            continue;
        }
        if (pull->bsf == NULL)
        {
            return BASE_OK;
        }
        ret = av_bsf_send_packet(pull->bsf, pull->packet);
        if (ret < 0)
        {
            return BASE_ERR;
        }
        ret = av_bsf_receive_packet(pull->bsf, pull->packet);
        if (ret == 0)
        {
            return BASE_OK;
        }
        if (ret == AVERROR(EAGAIN))
        {
            continue;
        }
        return BASE_ERR;
    }
}

static void Publish(const NetPull *pull, NetPacket *pkt)
{
    pkt->data = pull->frame;
    pkt->size = pull->frame_size;
    pkt->pts_us = ScaleUs(pull, pull->frame_pts);
    pkt->key = pull->frame_key;
    pkt->eos = 0;
}

static BaseStatus ReadOne(NetPull *pull, NetPacket *pkt)
{
    for (;;)
    {
        int got = 0;
        BaseStatus status;
        if (atomic_load(&pull->stop) != 0)
        {
            return BASE_ERR;
        }
        if (pull->tail_size > 0)
        {
            uint8_t *data = pull->tail;
            int size = pull->tail_size;
            int64_t pts = pull->tail_pts;
            pull->tail = NULL;
            pull->tail_size = 0;
            status = Feed(pull, data, size, pts, &got);
            free(data);
            if (status != BASE_OK)
            {
                return status;
            }
            if (got)
            {
                Publish(pull, pkt);
                return BASE_OK;
            }
            continue;
        }
        status = NextVideo(pull);
        if (status != BASE_OK)
        {
            return status;
        }
        {
            int64_t pts = NextStamp(pull, pull->packet->pts, pull->packet->dts);
            status = Feed(pull, pull->packet->data, pull->packet->size, pts, &got);
            av_packet_unref(pull->packet);
            if (status != BASE_OK)
            {
                return status;
            }
            if (got)
            {
                Publish(pull, pkt);
                return BASE_OK;
            }
        }
    }
}

static int VideoIndex(const AVFormatContext *fmt)
{
    unsigned int i;
    for (i = 0; i < fmt->nb_streams; i++)
    {
        const AVStream *stream = fmt->streams[i];
        if (stream != NULL && stream->codecpar != NULL && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            return (int)i;
        }
    }
    return -1;
}

static BaseStatus Fail(NetPull *pull, NetPull **out, BaseStatus status)
{
    Destroy(pull);
    *out = NULL;
    return status;
}

BaseStatus net_open(const NetSource *source, int transport, NetPull **out)
{
    NetPull *pull;
    AVDictionary *opts = NULL;
    char timeout[32];
    int ret;
    int index;
    if (out == NULL)
    {
        return BASE_ERR;
    }
    *out = NULL;
    if (source == NULL || source->url[0] == '\0' || source->timeout_ms <= 0)
    {
        return BASE_ERR;
    }
    if (transport != NET_TRANSPORT_TCP && transport != NET_TRANSPORT_UDP)
    {
        return BASE_ERR;
    }
    pull = calloc(1, sizeof(*pull));
    if (pull == NULL)
    {
        return BASE_ERR;
    }
    if (pthread_mutex_init(&pull->lock, NULL) != 0)
    {
        free(pull);
        return BASE_ERR;
    }
    atomic_init(&pull->stop, 0);
    pthread_once(&g_network_once, InitNetwork);
    pull->packet = av_packet_alloc();
    pull->fmt = avformat_alloc_context();
    if (pull->packet == NULL || pull->fmt == NULL)
    {
        return Fail(pull, out, BASE_ERR);
    }
    pull->fmt->interrupt_callback.callback = Interrupt;
    pull->fmt->interrupt_callback.opaque = pull;
    snprintf(timeout, sizeof(timeout), "%lld", (long long)source->timeout_ms * 1000LL);
    av_dict_set(&opts, "stimeout", timeout, 0);
    av_dict_set(&opts, "rtsp_transport", transport == NET_TRANSPORT_UDP ? "udp" : "tcp", 0);
    ret = avformat_open_input(&pull->fmt, source->url, NULL, &opts);
    av_dict_free(&opts);
    if (ret < 0)
    {
        return Fail(pull, out, BASE_ERR);
    }
    ret = avformat_find_stream_info(pull->fmt, NULL);
    if (ret < 0)
    {
        return Fail(pull, out, BASE_ERR);
    }
    index = VideoIndex(pull->fmt);
    if (index < 0)
    {
        return Fail(pull, out, BASE_ERR);
    }
    pull->video_index = index;
    ret = Adopt(pull, pull->fmt->streams[index]);
    if (ret != BASE_OK)
    {
        return Fail(pull, out, ret);
    }
    *out = pull;
    return BASE_OK;
}

void net_close(NetPull *pull)
{
    if (pull == NULL)
    {
        return;
    }
    atomic_store(&pull->stop, 1);
    pthread_mutex_lock(&pull->lock);
    Release(pull);
    pthread_mutex_unlock(&pull->lock);
    pthread_mutex_destroy(&pull->lock);
    free(pull);
}

BaseStatus net_info(const NetPull *pull, NetInfo *out)
{
    if (pull == NULL || out == NULL)
    {
        return BASE_ERR;
    }
    out->codec = pull->codec;
    out->width = pull->width;
    out->height = pull->height;
    out->extradata = pull->extra;
    out->extradata_size = pull->extra_size;
    return BASE_OK;
}

BaseStatus net_read(NetPull *pull, NetPacket *pkt)
{
    BaseStatus status;
    if (pull == NULL || pkt == NULL)
    {
        return BASE_ERR;
    }
    pthread_mutex_lock(&pull->lock);
    if (pull->dead || atomic_load(&pull->stop) != 0)
    {
        pthread_mutex_unlock(&pull->lock);
        return BASE_ERR;
    }
    status = ReadOne(pull, pkt);
    if (status == BASE_OK && atomic_load(&pull->stop) != 0)
    {
        status = BASE_ERR;
    }
    if (status != BASE_OK)
    {
        pull->dead = 1;
    }
    pthread_mutex_unlock(&pull->lock);
    return status;
}
