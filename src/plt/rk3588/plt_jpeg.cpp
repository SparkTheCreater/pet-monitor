/**
 * @file    plt_jpeg.cpp
 * @brief   把一帧 NV12 同步编成 JPEG。有限范围先换成全范围。
 */
#include "plt/plt_jpeg.h"
#include "plt/plt_mem.h"
#include "plt/plt_rga.h"

#include "plt_internal.h"

#include "mpp_frame.h"
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

    Encoder()
        : ctx(nullptr)
        , mpi(nullptr)
        , cfg(nullptr)
        , frame(nullptr)
    {
    }

    ~Encoder()
    {
        if (frame != nullptr)
        {
            mpp_frame_deinit(&frame);
        }
        if (cfg != nullptr)
        {
            mpp_enc_cfg_deinit(cfg);
        }
        if (ctx != nullptr)
        {
            mpp_destroy(ctx);
        }
    }
};

PmStatus EncodeFull(const PmImage *image, int quality, uint8_t **jpeg, size_t *jpeg_len)
{
    Encoder enc;
    if (mpp_create(&enc.ctx, &enc.mpi) != MPP_OK)
    {
        return PM_ERR;
    }
    RK_S64 timeout = MPP_POLL_BLOCK;
    enc.mpi->control(enc.ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout);
    if (mpp_init(enc.ctx, MPP_CTX_ENC, MPP_VIDEO_CodingMJPEG) != MPP_OK)
    {
        return PM_ERR;
    }
    if (mpp_enc_cfg_init(&enc.cfg) != MPP_OK)
    {
        return PM_ERR;
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
    mpp_enc_cfg_set_s32(enc.cfg, "jpeg:q_factor", quality);
    mpp_enc_cfg_set_s32(enc.cfg, "jpeg:qf_max", 99);
    mpp_enc_cfg_set_s32(enc.cfg, "jpeg:qf_min", 1);
    mpp_enc_cfg_set_s32(enc.cfg, "codec:type", MPP_VIDEO_CodingMJPEG);
    if (enc.mpi->control(enc.ctx, MPP_ENC_SET_CFG, enc.cfg) != MPP_OK)
    {
        fprintf(stderr, "[plt.jpeg] set config failed\n");
        return PM_ERR;
    }

    if (mpp_frame_init(&enc.frame) != MPP_OK)
    {
        return PM_ERR;
    }
    mpp_frame_set_width(enc.frame, (RK_U32)image->width);
    mpp_frame_set_height(enc.frame, (RK_U32)image->height);
    mpp_frame_set_hor_stride(enc.frame, (RK_U32)image->hor_stride);
    mpp_frame_set_ver_stride(enc.frame, (RK_U32)image->ver_stride);
    mpp_frame_set_fmt(enc.frame, MPP_FMT_YUV420SP);
    mpp_frame_set_color_range(enc.frame, MPP_FRAME_RANGE_JPEG);
    mpp_frame_set_buffer(enc.frame, (MppBuffer)image->handle);

    if (enc.mpi->encode_put_frame(enc.ctx, enc.frame) != MPP_OK)
    {
        fprintf(stderr, "[plt.jpeg] put frame failed\n");
        return PM_ERR;
    }
    MppPacket packet = nullptr;
    if (enc.mpi->encode_get_packet(enc.ctx, &packet) != MPP_OK || packet == nullptr)
    {
        if (packet != nullptr)
        {
            mpp_packet_deinit(&packet);
        }
        fprintf(stderr, "[plt.jpeg] get packet failed\n");
        return PM_ERR;
    }

    const size_t len = mpp_packet_get_length(packet);
    void *pos = mpp_packet_get_pos(packet);
    if (pos == nullptr || len == 0)
    {
        mpp_packet_deinit(&packet);
        return PM_ERR;
    }
    uint8_t *copy = (uint8_t *)malloc(len);
    if (copy == nullptr)
    {
        mpp_packet_deinit(&packet);
        return PM_ERR;
    }
    memcpy(copy, pos, len);
    mpp_packet_deinit(&packet);
    *jpeg = copy;
    *jpeg_len = len;
    return PM_OK;
}

}

extern "C" PmStatus plt_jpeg_encode(const PmImage *image, int quality, uint8_t **jpeg, size_t *jpeg_len)
{
    if (jpeg != nullptr)
    {
        *jpeg = nullptr;
    }
    if (jpeg_len != nullptr)
    {
        *jpeg_len = 0;
    }
    if (!pm_image_valid(image) || jpeg == nullptr || jpeg_len == nullptr)
    {
        return PM_ERR;
    }
    if (image->format != PM_PIX_NV12 || quality < 1 || quality > 100 || !plt_rk_ready())
    {
        return PM_ERR;
    }

    if (image->range == PM_RANGE_FULL)
    {
        return EncodeFull(image, quality, jpeg, jpeg_len);
    }

    PmImage full;
    memset(&full, 0, sizeof(full));
    full.fd = -1;
    if (plt_mem_alloc(image->width, image->height, PM_PIX_NV12, &full) != PM_OK)
    {
        return PM_ERR;
    }
    /* 目标标成全范围，转换按这个系数把有限范围拉开。 */
    full.range = PM_RANGE_FULL;
    if (plt_rga_blit(image, nullptr, &full, nullptr) != PM_OK)
    {
        plt_mem_free(&full);
        fprintf(stderr, "[plt.jpeg] expand range failed\n");
        return PM_ERR;
    }
    const PmStatus status = EncodeFull(&full, quality, jpeg, jpeg_len);
    plt_mem_free(&full);
    return status;
}

extern "C" void plt_jpeg_free(uint8_t *jpeg)
{
    free(jpeg);
}
