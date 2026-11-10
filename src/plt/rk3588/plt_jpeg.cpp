/**
 * @file    plt_jpeg.cpp
 * @brief   把一帧 NV12 同步编成 JPEG。有限范围先换成全范围。
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
#include "rk_venc_cfg.h"
#include "rk_venc_rc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{

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

extern "C" void plt_jpeg_free(uint8_t *jpeg)
{
    free(jpeg);
}
