/**
 * @file    plt_venc.cpp
 * @brief   MPP 视频编码。输入帧直接挂已有的 DRM buffer，不再拷贝像素。
 */
#include "plt/plt_venc.h"

#include "plt_internal.h"

#include "mpp_buffer.h"
#include "mpp_frame.h"
#include "mpp_meta.h"
#include "mpp_packet.h"
#include "rk_mpi.h"
#include "rk_venc_cfg.h"
#include "rk_venc_cmd.h"
#include "rk_venc_rc.h"

#include <stdio.h>
#include <string.h>

#include <deque>
#include <new>
#include <utility>
#include <vector>

struct EncPacket
{
    std::vector<uint8_t> data;
    int64_t pts_us;
    int key;
};

struct PltVenc
{
    MppCtx ctx;
    MppApi *mpi;
    MppEncCfg cfg;
    MppBufferGroup group;
    MppBuffer packet_buf;
    int width;
    int height;
    int hor_stride;
    int ver_stride;
    int format;
    int rc_mode;
    bool mismatch_logged;
    std::deque<EncPacket> queue;
    EncPacket held;
    int held_valid;
    std::vector<MppEncROIRegion> roi;

    PltVenc()
        : ctx(nullptr)
        , mpi(nullptr)
        , cfg(nullptr)
        , group(nullptr)
        , packet_buf(nullptr)
        , width(0)
        , height(0)
        , hor_stride(0)
        , ver_stride(0)
        , format(0)
        , rc_mode(0)
        , mismatch_logged(false)
        , held_valid(0)
    {
    }
};

namespace
{

int Align16(int value)
{
    return (value + 15) & ~15;
}

MppFrameFormat MppFormat(int format)
{
    return format == BASE_PIX_RGB888 ? MPP_FMT_RGB888 : MPP_FMT_YUV420SP;
}

/* 图像的 hor_stride 按像素。MPP 的 RGB 行宽按字节，YUV 按像素。 */
int StrideBytes(int format, int hor_stride)
{
    return format == BASE_PIX_RGB888 ? hor_stride * 3 : hor_stride;
}

int ParamOk(const PltVencParam *param)
{
    int common;
    if (param == nullptr)
    {
        return 0;
    }
    common = (param->codec == BASE_CODEC_H264 || param->codec == BASE_CODEC_H265) &&
             (param->format == BASE_PIX_NV12 || param->format == BASE_PIX_RGB888) &&
             param->width > 0 && param->height > 0 && param->fps > 0 && param->gop > 0 &&
             param->bitrate > 0 &&
             (param->rc_mode == PLT_RC_CBR || param->rc_mode == PLT_RC_VBR) &&
             param->qp_min >= 0 && param->qp_min <= param->qp_init && param->qp_init <= param->qp_max &&
             param->qp_max <= 51;
    if (!common || param->codec != BASE_CODEC_H264)
    {
        return common;
    }
    return (param->profile == 66 || param->profile == 77 || param->profile == 100) &&
           param->level >= 10 && param->level <= 52;
}

/* CBR 上下限收得窄，VBR 下限放宽，取值同 MPP 示例。 */
void SetBitrate(MppEncCfg cfg, int rc_mode, int bitrate)
{
    mpp_enc_cfg_set_s32(cfg, "rc:bps_target", bitrate);
    mpp_enc_cfg_set_s32(cfg, "rc:bps_max", (int)((int64_t)bitrate * 17 / 16));
    mpp_enc_cfg_set_s32(cfg, "rc:bps_min", (int)((int64_t)bitrate * (rc_mode == PLT_RC_CBR ? 15 : 1) / 16));
}

void PushPacket(PltVenc *enc, MppPacket packet, int64_t fallback_pts)
{
    void *pos = mpp_packet_get_pos(packet);
    size_t len = mpp_packet_get_length(packet);
    EncPacket item;
    RK_S32 intra = 0;
    if (pos == nullptr || len == 0)
    {
        return;
    }
    item.data.assign((uint8_t *)pos, (uint8_t *)pos + len);
    item.pts_us = mpp_packet_get_pts(packet);
    if (item.pts_us <= 0)
    {
        item.pts_us = fallback_pts;
    }
    /* 编码器在包的 meta 里标了是否帧内编码，比扫 NAL 头准确。 */
    if (mpp_packet_has_meta(packet))
    {
        mpp_meta_get_s32(mpp_packet_get_meta(packet), KEY_OUTPUT_INTRA, &intra);
    }
    item.key = intra != 0;
    enc->queue.push_back(std::move(item));
}

}

extern "C" void plt_venc_close(PltVenc *enc)
{
    if (enc == nullptr)
    {
        return;
    }
    /* 按 SDK 示例的顺序：先停编码器、销毁上下文，再放码流缓冲。 */
    if (enc->mpi != nullptr && enc->ctx != nullptr)
    {
        enc->mpi->reset(enc->ctx);
    }
    if (enc->ctx != nullptr)
    {
        mpp_destroy(enc->ctx);
    }
    if (enc->cfg != nullptr)
    {
        mpp_enc_cfg_deinit(enc->cfg);
    }
    if (enc->packet_buf != nullptr)
    {
        mpp_buffer_put(enc->packet_buf);
    }
    if (enc->group != nullptr)
    {
        mpp_buffer_group_put(enc->group);
    }
    delete enc;
}

extern "C" BaseStatus plt_venc_open(const PltVencParam *param, PltVenc **out)
{
    PltVenc *enc;
    MppCodingType coding;
    MppEncHeaderMode header_mode;
    MppPacket packet;
    if (out == nullptr || !plt_rk_ready())
    {
        if (out != nullptr)
        {
            *out = nullptr;
        }
        return BASE_ERR;
    }
    *out = nullptr;
    if (!ParamOk(param))
    {
        if (param == nullptr)
        {
            return BASE_ERR;
        }
        fprintf(stderr,
                "[plt.venc] bad param codec=%d fmt=%d %dx%d fps=%d gop=%d rc=%d bps=%d qp=%d/%d/%d profile=%d level=%d\n",
                param->codec,
                param->format,
                param->width,
                param->height,
                param->fps,
                param->gop,
                param->rc_mode,
                param->bitrate,
                param->qp_init,
                param->qp_min,
                param->qp_max,
                param->profile,
                param->level);
        return BASE_ERR;
    }

    enc = new (std::nothrow) PltVenc();
    if (enc == nullptr)
    {
        return BASE_ERR;
    }
    enc->width = param->width;
    enc->height = param->height;
    enc->hor_stride = Align16(param->width);
    enc->ver_stride = Align16(param->height);
    enc->format = param->format;
    enc->rc_mode = param->rc_mode;

    coding = param->codec == BASE_CODEC_H265 ? MPP_VIDEO_CodingHEVC : MPP_VIDEO_CodingAVC;
    if (mpp_create(&enc->ctx, &enc->mpi) != MPP_OK || mpp_init(enc->ctx, MPP_CTX_ENC, coding) != MPP_OK)
    {
        plt_venc_close(enc);
        return BASE_ERR;
    }
    if (mpp_enc_cfg_init(&enc->cfg) != MPP_OK)
    {
        plt_venc_close(enc);
        return BASE_ERR;
    }

    mpp_enc_cfg_set_s32(enc->cfg, "prep:width", param->width);
    mpp_enc_cfg_set_s32(enc->cfg, "prep:height", param->height);
    mpp_enc_cfg_set_s32(enc->cfg, "prep:hor_stride", StrideBytes(param->format, enc->hor_stride));
    mpp_enc_cfg_set_s32(enc->cfg, "prep:ver_stride", enc->ver_stride);
    mpp_enc_cfg_set_s32(enc->cfg, "prep:format", MppFormat(param->format));
    mpp_enc_cfg_set_s32(enc->cfg, "rc:mode", param->rc_mode == PLT_RC_CBR ? MPP_ENC_RC_MODE_CBR : MPP_ENC_RC_MODE_VBR);
    mpp_enc_cfg_set_s32(enc->cfg, "rc:fps_in_num", param->fps);
    mpp_enc_cfg_set_s32(enc->cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(enc->cfg, "rc:fps_out_num", param->fps);
    mpp_enc_cfg_set_s32(enc->cfg, "rc:fps_out_denorm", 1);
    mpp_enc_cfg_set_s32(enc->cfg, "rc:gop", param->gop);
    SetBitrate(enc->cfg, param->rc_mode, param->bitrate);
    mpp_enc_cfg_set_s32(enc->cfg, "rc:qp_init", param->qp_init);
    mpp_enc_cfg_set_s32(enc->cfg, "rc:qp_max", param->qp_max);
    mpp_enc_cfg_set_s32(enc->cfg, "rc:qp_min", param->qp_min);
    mpp_enc_cfg_set_s32(enc->cfg, "rc:qp_max_i", param->qp_max);
    mpp_enc_cfg_set_s32(enc->cfg, "rc:qp_min_i", param->qp_min);
    mpp_enc_cfg_set_s32(enc->cfg, "rc:qp_ip", 2);
    mpp_enc_cfg_set_s32(enc->cfg, "codec:type", coding);
    if (coding == MPP_VIDEO_CodingAVC)
    {
        /* Baseline 不支持 CABAC，只有 High 支持 8x8 变换。 */
        mpp_enc_cfg_set_s32(enc->cfg, "h264:profile", param->profile);
        mpp_enc_cfg_set_s32(enc->cfg, "h264:level", param->level);
        mpp_enc_cfg_set_s32(enc->cfg, "h264:cabac_en", param->profile >= 77 ? 1 : 0);
        mpp_enc_cfg_set_s32(enc->cfg, "h264:cabac_idc", 0);
        mpp_enc_cfg_set_s32(enc->cfg, "h264:trans8x8", param->profile >= 100 ? 1 : 0);
    }
    if (enc->mpi->control(enc->ctx, MPP_ENC_SET_CFG, enc->cfg) != MPP_OK)
    {
        fprintf(stderr, "[plt.venc] set cfg failed\n");
        plt_venc_close(enc);
        return BASE_ERR;
    }

    /* 每个关键帧都带参数集，中途接入的接收端不用等下一个 GOP。 */
    header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    enc->mpi->control(enc->ctx, MPP_ENC_SET_HEADER_MODE, &header_mode);

    if (mpp_buffer_group_get_internal(&enc->group, MPP_BUFFER_TYPE_DRM) != MPP_OK ||
        mpp_buffer_get(enc->group, &enc->packet_buf, (size_t)enc->hor_stride * (size_t)enc->ver_stride) != MPP_OK)
    {
        fprintf(stderr, "[plt.venc] packet buffer failed\n");
        plt_venc_close(enc);
        return BASE_ERR;
    }

    /* 先让编码器写出参数集。关键帧模式会把它附在每个 IDR 上。 */
    packet = nullptr;
    if (mpp_packet_init_with_buffer(&packet, enc->packet_buf) == MPP_OK)
    {
        mpp_packet_set_length(packet, 0);
        enc->mpi->control(enc->ctx, MPP_ENC_GET_HDR_SYNC, packet);
        mpp_packet_deinit(&packet);
    }

    *out = enc;
    return BASE_OK;
}

extern "C" BaseStatus plt_venc_send(PltVenc *enc, const BaseImage *image, int64_t pts_us)
{
    MppFrame frame = nullptr;
    MppEncROICfg roi_cfg;
    MppPacket packet = nullptr;
    MPP_RET put_ret;
    BaseStatus status = BASE_ERR;
    int i;
    if (enc == nullptr || !base_image_valid(image) || image->format != enc->format)
    {
        return BASE_ERR;
    }
    /* 编码器按 open 时的宽高和行宽读，帧不一致会读错行甚至越界。 */
    if (image->width != enc->width || image->height != enc->height ||
        image->hor_stride != enc->hor_stride || image->ver_stride != enc->ver_stride)
    {
        if (!enc->mismatch_logged)
        {
            enc->mismatch_logged = true;
            fprintf(stderr,
                    "[plt.venc] frame %dx%d stride=%dx%d, encoder wants %dx%d stride=%dx%d\n",
                    image->width,
                    image->height,
                    image->hor_stride,
                    image->ver_stride,
                    enc->width,
                    enc->height,
                    enc->hor_stride,
                    enc->ver_stride);
        }
        return BASE_ERR;
    }

    if (mpp_frame_init(&frame) != MPP_OK)
    {
        return BASE_ERR;
    }
    mpp_frame_set_width(frame, (RK_U32)enc->width);
    mpp_frame_set_height(frame, (RK_U32)enc->height);
    mpp_frame_set_hor_stride(frame, (RK_U32)StrideBytes(image->format, image->hor_stride));
    mpp_frame_set_ver_stride(frame, (RK_U32)image->ver_stride);
    mpp_frame_set_fmt(frame, MppFormat(image->format));
    mpp_frame_set_pts(frame, pts_us);
    /*
     * handle 就是这块图的 MppBuffer。编码直接引用这块 fd。
     * put_frame 返回后帧对象可以销毁，帧引用由编码自己持有到取完码流。
     * 调用方在 send 返回前不能再改这块 fd。
     */
    mpp_frame_set_buffer(frame, (MppBuffer)image->handle);

    /* 区域数组要活到这一帧编码结束。HAL 在送帧过程中按这个指针读。 */
    memset(&roi_cfg, 0, sizeof(roi_cfg));
    if (!enc->roi.empty())
    {
        roi_cfg.number = (RK_U32)enc->roi.size();
        roi_cfg.regions = enc->roi.data();
        mpp_meta_set_ptr(mpp_frame_get_meta(frame), KEY_ROI_DATA, &roi_cfg);
    }

    if (mpp_packet_init_with_buffer(&packet, enc->packet_buf) != MPP_OK)
    {
        mpp_frame_deinit(&frame);
        return BASE_ERR;
    }
    mpp_packet_set_length(packet, 0);
    mpp_meta_set_packet(mpp_frame_get_meta(frame), KEY_OUTPUT_PACKET, packet);

    put_ret = enc->mpi->encode_put_frame(enc->ctx, frame);
    mpp_frame_deinit(&frame);
    if (put_ret != MPP_OK)
    {
        mpp_packet_deinit(&packet);
        fprintf(stderr, "[plt.venc] put frame failed %d\n", put_ret);
        return BASE_ERR;
    }

    /*
     * 同一块 packet 取码流。分区编码时 get 可能返回多段，直到 EOI。
     * 取完再释放，避免和 KEY_OUTPUT_PACKET 各释放一次。
     */
    for (i = 0; i < 8; ++i)
    {
        MPP_RET get_ret = enc->mpi->encode_get_packet(enc->ctx, &packet);
        int eoi;
        if (get_ret != MPP_OK || packet == nullptr)
        {
            fprintf(stderr, "[plt.venc] get packet failed %d\n", get_ret);
            break;
        }
        PushPacket(enc, packet, pts_us);
        eoi = !mpp_packet_is_partition(packet) || mpp_packet_is_eoi(packet);
        mpp_packet_deinit(&packet);
        if (eoi)
        {
            status = BASE_OK;
            break;
        }
    }
    if (packet != nullptr)
    {
        mpp_packet_deinit(&packet);
    }
    return status;
}

extern "C" BaseStatus plt_venc_get(PltVenc *enc, PltVencPacket *packet)
{
    if (enc == nullptr || packet == nullptr)
    {
        return BASE_ERR;
    }
    if (!enc->held_valid)
    {
        if (enc->queue.empty())
        {
            return BASE_AGAIN;
        }
        enc->held = std::move(enc->queue.front());
        enc->queue.pop_front();
        enc->held_valid = 1;
    }
    packet->data = enc->held.data.data();
    packet->size = enc->held.data.size();
    packet->pts_us = enc->held.pts_us;
    packet->key = enc->held.key;
    return BASE_OK;
}

extern "C" void plt_venc_get_done(PltVenc *enc)
{
    if (enc == nullptr)
    {
        return;
    }
    enc->held_valid = 0;
    enc->held.data.clear();
}

extern "C" BaseStatus plt_venc_request_idr(PltVenc *enc)
{
    if (enc == nullptr || enc->mpi == nullptr)
    {
        return BASE_ERR;
    }
    return enc->mpi->control(enc->ctx, MPP_ENC_SET_IDR_FRAME, nullptr) == MPP_OK ? BASE_OK : BASE_ERR;
}

extern "C" BaseStatus plt_venc_set_bitrate(PltVenc *enc, int bitrate)
{
    MppEncCfg cfg = nullptr;
    int ok;
    if (enc == nullptr || enc->mpi == nullptr || bitrate <= 0)
    {
        return BASE_ERR;
    }
    /* 先取回当前配置，只改码率这几项再写回。 */
    if (mpp_enc_cfg_init(&cfg) != MPP_OK)
    {
        return BASE_ERR;
    }
    ok = enc->mpi->control(enc->ctx, MPP_ENC_GET_CFG, cfg) == MPP_OK;
    if (ok)
    {
        SetBitrate(cfg, enc->rc_mode, bitrate);
        ok = enc->mpi->control(enc->ctx, MPP_ENC_SET_CFG, cfg) == MPP_OK;
    }
    mpp_enc_cfg_deinit(cfg);
    if (!ok)
    {
        fprintf(stderr, "[plt.venc] set bitrate %d failed\n", bitrate);
        return BASE_ERR;
    }
    return BASE_OK;
}

extern "C" BaseStatus plt_venc_set_roi(PltVenc *enc, const PltVencRoi *regions, int count)
{
    std::vector<MppEncROIRegion> next;
    int i;
    if (enc == nullptr || count < 0 || count > 8 || (count > 0 && regions == nullptr))
    {
        return BASE_ERR;
    }
    next.reserve((size_t)count);
    for (i = 0; i < count; ++i)
    {
        const PltVencRoi *src = &regions[i];
        int inside = src->x >= 0 && src->y >= 0 && src->w > 0 && src->h > 0 &&
                     src->x <= enc->width && src->w <= enc->width - src->x &&
                     src->y <= enc->height && src->h <= enc->height - src->y;
        int aligned = (src->x & 15) == 0 && (src->y & 15) == 0 && (src->w & 15) == 0 && (src->h & 15) == 0;
        int qp_ok = (src->abs_qp == 0 && src->qp >= -51 && src->qp <= 51) ||
                    (src->abs_qp == 1 && src->qp >= 0 && src->qp <= 51);
        MppEncROIRegion region;
        if (!inside || !aligned || !qp_ok || (src->intra != 0 && src->intra != 1))
        {
            return BASE_ERR;
        }
        memset(&region, 0, sizeof(region));
        region.x = (RK_U16)src->x;
        region.y = (RK_U16)src->y;
        region.w = (RK_U16)src->w;
        region.h = (RK_U16)src->h;
        region.intra = (RK_U16)src->intra;
        region.quality = (RK_S16)src->qp;
        region.area_map_en = 1;
        region.abs_qp_en = (RK_U8)src->abs_qp;
        next.push_back(region);
    }
    enc->roi.swap(next);
    return BASE_OK;
}
