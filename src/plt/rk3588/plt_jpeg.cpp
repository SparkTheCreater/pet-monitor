/**
 * @file    plt_jpeg.cpp
 * @brief   整张 JPEG。解码走 mpi_dec_test 的任务模式，编码把有限范围先换成全范围。
 */
#include "plt/plt_jpeg.h"
#include "plt/plt_mem.h"
#include "plt/plt_rga.h"

#include "plt_internal.h"

#include "mpp_buffer.h"
#include "mpp_frame.h"
#include "mpp_meta.h"
#include "mpp_packet.h"
#include "rk_mpi.h"
#include "rk_vdec_cfg.h"
#include "rk_venc_cfg.h"
#include "rk_venc_rc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{

/* 坏图可能不出帧。等太久就当失败，避免卡住调用方。上限见 MPP_POLL_MAX。 */
const int kOutputWaitMs = 1000;

/* 宽高在 SOF 里。渐进式也是 SOF，交给解码器拒绝。 */
int JpegSize(const uint8_t *data, size_t size, int *width, int *height)
{
    size_t pos;
    if (data == nullptr || size < 4 || data[0] != 0xff || data[1] != 0xd8)
    {
        return 0;
    }
    pos = 2;
    while (pos + 4 <= size)
    {
        int marker;
        int segment;
        int sof;
        if (data[pos] != 0xff)
        {
            return 0;
        }
        marker = data[pos + 1];
        if (marker == 0xff)
        {
            pos++;
            continue;
        }
        segment = (data[pos + 2] << 8) | data[pos + 3];
        sof = marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
        if (sof)
        {
            if (pos + 9 > size)
            {
                return 0;
            }
            *height = (data[pos + 5] << 8) | data[pos + 6];
            *width = (data[pos + 7] << 8) | data[pos + 8];
            return *width > 0 && *height > 0;
        }
        if (segment < 2)
        {
            return 0;
        }
        pos += 2 + (size_t)segment;
    }
    return 0;
}

struct Decoder
{
    MppCtx ctx;
    MppApi *mpi;
    MppDecCfg cfg;
    MppBufferGroup group;
    MppBuffer stream;
    MppPacket packet;
    MppFrame frame;

    Decoder()
        : ctx(nullptr)
        , mpi(nullptr)
        , cfg(nullptr)
        , group(nullptr)
        , stream(nullptr)
        , packet(nullptr)
        , frame(nullptr)
    {
    }

    ~Decoder()
    {
        /* 先停解码器，它手里的包和帧引用放掉后，再拆缓冲。 */
        if (ctx != nullptr)
        {
            if (mpi != nullptr)
            {
                mpi->reset(ctx);
            }
            mpp_destroy(ctx);
        }
        if (frame != nullptr)
        {
            mpp_frame_deinit(&frame);
        }
        if (packet != nullptr)
        {
            mpp_packet_deinit(&packet);
        }
        if (stream != nullptr)
        {
            mpp_buffer_put(stream);
        }
        if (group != nullptr)
        {
            mpp_buffer_group_put(group);
        }
        if (cfg != nullptr)
        {
            mpp_dec_cfg_deinit(cfg);
        }
    }
};

/*
 * jpegd 直接让硬件读包里的 buffer，码流要放进和图像一样的设备缓冲。
 * 硬件按 16 字节对齐读，尾部留一段零。
 */
BaseStatus LoadStream(Decoder *dec, const uint8_t *data, size_t size)
{
    const size_t bytes = size + 256;
    int type = plt_rk_dma_type();
    uint8_t *ptr;
    BaseImage view;
    if (type == 0)
    {
        type = MPP_BUFFER_TYPE_DRM;
    }
    if (mpp_buffer_group_get_internal(&dec->group, (MppBufferType)type) != MPP_OK ||
        mpp_buffer_get(dec->group, &dec->stream, bytes) != MPP_OK)
    {
        fprintf(stderr, "[plt.jpeg] stream buffer failed\n");
        return BASE_ERR;
    }
    ptr = (uint8_t *)mpp_buffer_get_ptr(dec->stream);
    if (ptr == nullptr)
    {
        return BASE_ERR;
    }
    memcpy(ptr, data, size);
    memset(ptr + size, 0, 256);
    memset(&view, 0, sizeof(view));
    view.fd = mpp_buffer_get_fd(dec->stream);
    view.handle = dec->stream;
    if (plt_mem_flush(&view) != BASE_OK || mpp_packet_init_with_buffer(&dec->packet, dec->stream) != MPP_OK)
    {
        return BASE_ERR;
    }
    mpp_packet_set_size(dec->packet, size);
    mpp_packet_set_length(dec->packet, size);
    mpp_packet_set_eos(dec->packet);
    return BASE_OK;
}

BaseStatus OpenDecoder(Decoder *dec)
{
    MppFrameFormat fmt;
    if (mpp_create(&dec->ctx, &dec->mpi) != MPP_OK)
    {
        return BASE_ERR;
    }
    if (mpp_init(dec->ctx, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG) != MPP_OK)
    {
        return BASE_ERR;
    }
    if (mpp_dec_cfg_init(&dec->cfg) != MPP_OK ||
        dec->mpi->control(dec->ctx, MPP_DEC_GET_CFG, dec->cfg) != MPP_OK)
    {
        return BASE_ERR;
    }
    /* 送进来的就是一整张图，不要再切帧。 */
    mpp_dec_cfg_set_u32(dec->cfg, "base:split_parse", 0);
    if (dec->mpi->control(dec->ctx, MPP_DEC_SET_CFG, dec->cfg) != MPP_OK)
    {
        return BASE_ERR;
    }
    /* 4:2:2、4:4:4 由解码器转成 NV12。mpi_dec_test 在解 JPEG 前设置输出格式。 */
    fmt = MPP_FMT_YUV420SP;
    if (dec->mpi->control(dec->ctx, MPP_DEC_SET_OUTPUT_FORMAT, &fmt) != MPP_OK)
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

/*
 * JPEG 走任务模式：输入任务同时带码流和输出帧，回来时像素已经写进 image。
 * 再把输入任务收回，包才能释放。
 */
BaseStatus RunDecode(Decoder *dec, BaseImage *image)
{
    MppTask task = nullptr;
    MppFrame done = nullptr;
    RK_U32 fmt;
    if (mpp_frame_init(&dec->frame) != MPP_OK)
    {
        return BASE_ERR;
    }
    mpp_frame_set_buffer(dec->frame, (MppBuffer)image->handle);
    if (dec->mpi->poll(dec->ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK) != MPP_OK ||
        dec->mpi->dequeue(dec->ctx, MPP_PORT_INPUT, &task) != MPP_OK || task == nullptr)
    {
        return BASE_ERR;
    }
    mpp_task_meta_set_packet(task, KEY_INPUT_PACKET, dec->packet);
    mpp_task_meta_set_frame(task, KEY_OUTPUT_FRAME, dec->frame);
    if (dec->mpi->enqueue(dec->ctx, MPP_PORT_INPUT, task) != MPP_OK)
    {
        return BASE_ERR;
    }

    task = nullptr;
    if (dec->mpi->poll(dec->ctx, MPP_PORT_OUTPUT, (MppPollType)kOutputWaitMs) != MPP_OK ||
        dec->mpi->dequeue(dec->ctx, MPP_PORT_OUTPUT, &task) != MPP_OK || task == nullptr)
    {
        fprintf(stderr, "[plt.jpeg] no output frame\n");
        return BASE_ERR;
    }
    mpp_task_meta_get_frame(task, KEY_OUTPUT_FRAME, &done);
    if (dec->mpi->enqueue(dec->ctx, MPP_PORT_OUTPUT, task) != MPP_OK)
    {
        return BASE_ERR;
    }
    task = nullptr;
    if (dec->mpi->dequeue(dec->ctx, MPP_PORT_INPUT, &task) == MPP_OK && task != nullptr)
    {
        dec->mpi->enqueue(dec->ctx, MPP_PORT_INPUT, task);
    }

    if (done == nullptr || mpp_frame_get_errinfo(done) || mpp_frame_get_discard(done))
    {
        fprintf(stderr, "[plt.jpeg] decode error\n");
        return BASE_ERR;
    }
    /* 灰度图硬件不转成 NV12，按 NV12 读会花屏。 */
    fmt = mpp_frame_get_fmt(done) & MPP_FRAME_FMT_MASK;
    if (fmt != MPP_FMT_YUV420SP ||
        (int)mpp_frame_get_hor_stride(done) != image->hor_stride ||
        (int)mpp_frame_get_ver_stride(done) > image->ver_stride)
    {
        fprintf(stderr, "[plt.jpeg] unsupported output fmt=0x%x stride=%u\n",
                fmt,
                (unsigned)mpp_frame_get_hor_stride(done));
        return BASE_ERR;
    }
    return BASE_OK;
}

struct Encoder
{
    MppCtx ctx;
    MppApi *mpi;
    MppEncCfg cfg;
    MppFrame frame;
    MppBufferGroup group;
    MppBuffer packet_buf;

    Encoder()
        : ctx(nullptr)
        , mpi(nullptr)
        , cfg(nullptr)
        , frame(nullptr)
        , group(nullptr)
        , packet_buf(nullptr)
    {
    }

    ~Encoder()
    {
        if (frame != nullptr)
        {
            mpp_frame_deinit(&frame);
        }
        if (ctx != nullptr)
        {
            if (mpi != nullptr)
            {
                mpi->reset(ctx);
            }
            mpp_destroy(ctx);
        }
        if (cfg != nullptr)
        {
            mpp_enc_cfg_deinit(cfg);
        }
        if (packet_buf != nullptr)
        {
            mpp_buffer_put(packet_buf);
        }
        if (group != nullptr)
        {
            mpp_buffer_group_put(group);
        }
    }
};

/* 码流缓冲按 mpi_enc_test 的 frame_size。JPEG 一般比 NV12 小。 */
size_t PacketBytes(const BaseImage *image)
{
    const size_t hor = (size_t)((image->hor_stride + 63) & ~63);
    const size_t ver = (size_t)((image->ver_stride + 63) & ~63);
    return hor * ver * 3 / 2;
}

BaseStatus EncodeFull(const BaseImage *image, int quality, uint8_t **jpeg, size_t *jpeg_len)
{
    Encoder enc;
    if (mpp_create(&enc.ctx, &enc.mpi) != MPP_OK)
    {
        return BASE_ERR;
    }
    MppPollType timeout = MPP_POLL_BLOCK;
    enc.mpi->control(enc.ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout);
    if (mpp_init(enc.ctx, MPP_CTX_ENC, MPP_VIDEO_CodingMJPEG) != MPP_OK)
    {
        return BASE_ERR;
    }
    if (mpp_enc_cfg_init(&enc.cfg) != MPP_OK)
    {
        return BASE_ERR;
    }

    mpp_enc_cfg_set_s32(enc.cfg, "prep:width", image->width);
    mpp_enc_cfg_set_s32(enc.cfg, "prep:height", image->height);
    mpp_enc_cfg_set_s32(enc.cfg, "prep:hor_stride", image->hor_stride);
    mpp_enc_cfg_set_s32(enc.cfg, "prep:ver_stride", image->ver_stride);
    mpp_enc_cfg_set_s32(enc.cfg, "prep:format", MPP_FMT_YUV420SP);
    mpp_enc_cfg_set_s32(enc.cfg, "prep:range", MPP_FRAME_RANGE_JPEG);
    mpp_enc_cfg_set_s32(enc.cfg, "rc:mode", MPP_ENC_RC_MODE_FIXQP);
    mpp_enc_cfg_set_s32(enc.cfg, "rc:fps_in_num", 30);
    mpp_enc_cfg_set_s32(enc.cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(enc.cfg, "rc:fps_out_num", 30);
    mpp_enc_cfg_set_s32(enc.cfg, "rc:fps_out_denorm", 1);
    /* MPP 的 q_factor 只到 99。调用方给 100 时按最高质量送。 */
    mpp_enc_cfg_set_s32(enc.cfg, "jpeg:q_factor", quality > 99 ? 99 : quality);
    mpp_enc_cfg_set_s32(enc.cfg, "jpeg:qf_max", 99);
    mpp_enc_cfg_set_s32(enc.cfg, "jpeg:qf_min", 1);
    mpp_enc_cfg_set_s32(enc.cfg, "codec:type", MPP_VIDEO_CodingMJPEG);
    if (enc.mpi->control(enc.ctx, MPP_ENC_SET_CFG, enc.cfg) != MPP_OK)
    {
        fprintf(stderr, "[plt.jpeg] set config failed\n");
        return BASE_ERR;
    }

    if (mpp_frame_init(&enc.frame) != MPP_OK)
    {
        return BASE_ERR;
    }
    mpp_frame_set_width(enc.frame, (RK_U32)image->width);
    mpp_frame_set_height(enc.frame, (RK_U32)image->height);
    mpp_frame_set_hor_stride(enc.frame, (RK_U32)image->hor_stride);
    mpp_frame_set_ver_stride(enc.frame, (RK_U32)image->ver_stride);
    mpp_frame_set_fmt(enc.frame, MPP_FMT_YUV420SP);
    mpp_frame_set_color_range(enc.frame, MPP_FRAME_RANGE_JPEG);
    mpp_frame_set_eos(enc.frame, 1);
    mpp_frame_set_buffer(enc.frame, (MppBuffer)image->handle);

    const size_t bytes = PacketBytes(image);
    MppPacket packet = nullptr;
    if (bytes == 0 ||
        mpp_buffer_group_get_internal(&enc.group, MPP_BUFFER_TYPE_DRM) != MPP_OK ||
        mpp_buffer_get(enc.group, &enc.packet_buf, bytes) != MPP_OK ||
        mpp_packet_init_with_buffer(&packet, enc.packet_buf) != MPP_OK)
    {
        if (packet != nullptr)
        {
            mpp_packet_deinit(&packet);
        }
        fprintf(stderr, "[plt.jpeg] packet buffer failed\n");
        return BASE_ERR;
    }
    mpp_packet_set_length(packet, 0);
    mpp_meta_set_packet(mpp_frame_get_meta(enc.frame), KEY_OUTPUT_PACKET, packet);

    if (enc.mpi->encode_put_frame(enc.ctx, enc.frame) != MPP_OK)
    {
        mpp_packet_deinit(&packet);
        fprintf(stderr, "[plt.jpeg] put frame failed\n");
        return BASE_ERR;
    }
    mpp_frame_deinit(&enc.frame);

    /*
     * mpi_enc_test 在分区编码时一段一段取，直到 EOI。
     * 只取一次会在大图上拿到半张 JPEG。
     */
    uint8_t *copy = nullptr;
    size_t filled = 0;
    int limit = image->ver_stride / 16 + 2;
    if (limit < 2)
    {
        limit = 2;
    }
    for (int i = 0; i < limit; ++i)
    {
        if (enc.mpi->encode_get_packet(enc.ctx, &packet) != MPP_OK || packet == nullptr)
        {
            free(copy);
            fprintf(stderr, "[plt.jpeg] get packet failed\n");
            return BASE_ERR;
        }
        void *pos = mpp_packet_get_pos(packet);
        const size_t len = mpp_packet_get_length(packet);
        const int eoi = !mpp_packet_is_partition(packet) || mpp_packet_is_eoi(packet);
        if (len > 0)
        {
            uint8_t *grown = nullptr;
            if (pos == nullptr)
            {
                mpp_packet_deinit(&packet);
                free(copy);
                return BASE_ERR;
            }
            grown = (uint8_t *)realloc(copy, filled + len);
            if (grown == nullptr)
            {
                mpp_packet_deinit(&packet);
                free(copy);
                return BASE_ERR;
            }
            copy = grown;
            memcpy(copy + filled, pos, len);
            filled += len;
        }
        mpp_packet_deinit(&packet);
        if (eoi)
        {
            if (filled == 0)
            {
                free(copy);
                return BASE_ERR;
            }
            *jpeg = copy;
            *jpeg_len = filled;
            return BASE_OK;
        }
    }
    free(copy);
    fprintf(stderr, "[plt.jpeg] packet not finished\n");
    return BASE_ERR;
}

}

extern "C" BaseStatus plt_jpeg_encode(const BaseImage *image, int quality, uint8_t **jpeg, size_t *jpeg_len)
{
    if (jpeg != nullptr)
    {
        *jpeg = nullptr;
    }
    if (jpeg_len != nullptr)
    {
        *jpeg_len = 0;
    }
    if (!base_image_valid(image) || jpeg == nullptr || jpeg_len == nullptr)
    {
        return BASE_ERR;
    }
    if (image->format != BASE_PIX_NV12 || quality < 1 || quality > 100 || !plt_rk_ready())
    {
        return BASE_ERR;
    }

    if (image->range == BASE_RANGE_FULL)
    {
        return EncodeFull(image, quality, jpeg, jpeg_len);
    }

    BaseImage full;
    memset(&full, 0, sizeof(full));
    full.fd = -1;
    if (plt_mem_alloc(image->width, image->height, BASE_PIX_NV12, &full) != BASE_OK)
    {
        return BASE_ERR;
    }
    /* 目标标成全范围，转换按这个系数把有限范围拉开。 */
    full.range = BASE_RANGE_FULL;
    if (plt_rga_blit(image, nullptr, &full, nullptr) != BASE_OK)
    {
        plt_mem_free(&full);
        fprintf(stderr, "[plt.jpeg] expand range failed\n");
        return BASE_ERR;
    }
    const BaseStatus status = EncodeFull(&full, quality, jpeg, jpeg_len);
    plt_mem_free(&full);
    return status;
}

extern "C" BaseStatus plt_jpeg_decode(const uint8_t *data, size_t size, BaseImage *image)
{
    int width = 0;
    int height = 0;
    if (image != nullptr)
    {
        memset(image, 0, sizeof(*image));
        image->fd = -1;
    }
    if (data == nullptr || size == 0 || image == nullptr || !plt_rk_ready() ||
        !JpegSize(data, size, &width, &height))
    {
        return BASE_ERR;
    }
    if (plt_mem_alloc(width, height, BASE_PIX_NV12, image) != BASE_OK)
    {
        return BASE_ERR;
    }
    image->range = BASE_RANGE_FULL;
    Decoder dec;
    if (LoadStream(&dec, data, size) != BASE_OK ||
        OpenDecoder(&dec) != BASE_OK ||
        RunDecode(&dec, image) != BASE_OK)
    {
        plt_mem_free(image);
        return BASE_ERR;
    }
    return BASE_OK;
}

extern "C" void plt_jpeg_free(uint8_t *jpeg)
{
    free(jpeg);
}
