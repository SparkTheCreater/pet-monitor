/**
 * @file    media_enc.c
 * @brief   视频编码。参数检查在这里，编码交给平台层。
 */
#include "media_enc.h"

#include "plt/plt_venc.h"

#include <stdlib.h>
#include <string.h>

struct MediaEnc
{
    PltVenc *enc;
};

static int ParamOk(const MediaEncParam *param)
{
    if (param->codec != BASE_CODEC_H264 && param->codec != BASE_CODEC_H265)
    {
        return 0;
    }
    if (param->format != BASE_PIX_NV12 && param->format != BASE_PIX_RGB888)
    {
        return 0;
    }
    if (param->width <= 0 || param->height <= 0 || param->fps <= 0 || param->gop <= 0 || param->bitrate <= 0)
    {
        return 0;
    }
    if (param->rc_mode != MEDIA_RC_CBR && param->rc_mode != MEDIA_RC_VBR)
    {
        return 0;
    }
    if (param->qp_min < 0 || param->qp_max > 51 || param->qp_min > param->qp_init || param->qp_init > param->qp_max)
    {
        return 0;
    }
    if (param->codec == BASE_CODEC_H264 &&
        (param->profile != 66 && param->profile != 77 && param->profile != 100))
    {
        return 0;
    }
    if (param->codec == BASE_CODEC_H264 && (param->level < 10 || param->level > 52))
    {
        return 0;
    }
    return 1;
}

static void FillParam(const MediaEncParam *param, PltVencParam *out)
{
    memset(out, 0, sizeof(*out));
    out->codec = param->codec;
    out->format = param->format;
    out->width = param->width;
    out->height = param->height;
    out->fps = param->fps;
    out->gop = param->gop;
    out->rc_mode = param->rc_mode;
    out->bitrate = param->bitrate;
    out->qp_init = param->qp_init;
    out->qp_min = param->qp_min;
    out->qp_max = param->qp_max;
    out->profile = param->profile;
    out->level = param->level;
}

BaseStatus media_enc_open(const MediaEncParam *param, MediaEnc **out)
{
    PltVencParam plt_param;
    PltVenc *venc = NULL;
    MediaEnc *enc;
    BaseStatus status;
    if (out != NULL)
    {
        *out = NULL;
    }
    if (param == NULL || out == NULL || !ParamOk(param))
    {
        return BASE_ERR;
    }
    FillParam(param, &plt_param);
    status = plt_venc_open(&plt_param, &venc);
    if (status != BASE_OK)
    {
        return status;
    }
    enc = (MediaEnc *)calloc(1, sizeof(*enc));
    if (enc == NULL)
    {
        plt_venc_close(venc);
        return BASE_ERR;
    }
    enc->enc = venc;
    *out = enc;
    return BASE_OK;
}

void media_enc_close(MediaEnc *enc)
{
    if (enc == NULL)
    {
        return;
    }
    plt_venc_close(enc->enc);
    free(enc);
}

BaseStatus media_enc_extradata(MediaEnc *enc, const uint8_t **data, size_t *size)
{
    if (data != NULL)
    {
        *data = NULL;
    }
    if (size != NULL)
    {
        *size = 0;
    }
    if (enc == NULL)
    {
        return BASE_ERR;
    }
    return plt_venc_extradata(enc->enc, data, size);
}

BaseStatus media_enc_send(MediaEnc *enc, const BaseImage *image, int64_t pts_us)
{
    if (enc == NULL)
    {
        return BASE_ERR;
    }
    return plt_venc_send(enc->enc, image, pts_us);
}

BaseStatus media_enc_receive(MediaEnc *enc, MediaEncPacket *packet)
{
    PltVencPacket view;
    uint8_t *data;
    BaseStatus status;
    if (packet != NULL)
    {
        packet->data = NULL;
        packet->size = 0;
        packet->pts_us = 0;
        packet->key = 0;
    }
    if (enc == NULL || packet == NULL)
    {
        return BASE_ERR;
    }
    status = plt_venc_get(enc->enc, &view);
    if (status != BASE_OK)
    {
        return status;
    }
    data = (uint8_t *)malloc(view.size > 0 ? view.size : 1);
    if (data == NULL)
    {
        return BASE_ERR;
    }
    if (view.size > 0)
    {
        memcpy(data, view.data, view.size);
    }
    plt_venc_get_done(enc->enc);
    packet->data = data;
    packet->size = view.size;
    packet->pts_us = view.pts_us;
    packet->key = view.key;
    return BASE_OK;
}

void media_enc_packet_free(MediaEncPacket *packet)
{
    if (packet == NULL)
    {
        return;
    }
    free(packet->data);
    packet->data = NULL;
    packet->size = 0;
    packet->pts_us = 0;
    packet->key = 0;
}

BaseStatus media_enc_request_keyframe(MediaEnc *enc)
{
    if (enc == NULL)
    {
        return BASE_ERR;
    }
    return plt_venc_request_idr(enc->enc);
}

BaseStatus media_enc_set_bitrate(MediaEnc *enc, int bitrate)
{
    if (enc == NULL)
    {
        return BASE_ERR;
    }
    return plt_venc_set_bitrate(enc->enc, bitrate);
}

BaseStatus media_enc_set_rotation(MediaEnc *enc, int angle)
{
    if (enc == NULL)
    {
        return BASE_ERR;
    }
    return plt_venc_set_rotation(enc->enc, angle);
}

BaseStatus media_enc_set_mirror(MediaEnc *enc, int direction)
{
    if (enc == NULL)
    {
        return BASE_ERR;
    }
    return plt_venc_set_mirror(enc->enc, direction);
}

BaseStatus media_enc_set_roi(MediaEnc *enc, const MediaEncRoi *regions, int count)
{
    PltVencRoi local[8];
    const PltVencRoi *passed = NULL;
    int i;
    if (enc == NULL)
    {
        return BASE_ERR;
    }
    if (count > 0 && count <= 8 && regions != NULL)
    {
        for (i = 0; i < count; i++)
        {
            local[i].x = regions[i].x;
            local[i].y = regions[i].y;
            local[i].w = regions[i].w;
            local[i].h = regions[i].h;
            local[i].abs_qp = regions[i].abs_qp;
            local[i].qp = regions[i].qp;
            local[i].intra = regions[i].intra;
        }
        passed = local;
    }
    return plt_venc_set_roi(enc->enc, passed, count);
}

BaseStatus media_enc_set_osd_palette(MediaEnc *enc, const MediaEncOsdColor *colors, int count)
{
    PltVencOsdColor local[256];
    int i;
    if (enc == NULL)
    {
        return BASE_ERR;
    }
    if (count > 0 && count <= 256 && colors != NULL)
    {
        for (i = 0; i < count; i++)
        {
            local[i].y = colors[i].y;
            local[i].u = colors[i].u;
            local[i].v = colors[i].v;
            local[i].alpha = colors[i].alpha;
        }
        return plt_venc_set_osd_palette(enc->enc, local, count);
    }
    return plt_venc_set_osd_palette(enc->enc, NULL, count);
}

BaseStatus media_enc_set_osd(MediaEnc *enc, const MediaEncOsd *regions, int count)
{
    PltVencOsd local[8];
    const PltVencOsd *passed = NULL;
    int i;
    if (enc == NULL)
    {
        return BASE_ERR;
    }
    if (count > 0 && count <= 8 && regions != NULL)
    {
        for (i = 0; i < count; i++)
        {
            local[i].x = regions[i].x;
            local[i].y = regions[i].y;
            local[i].w = regions[i].w;
            local[i].h = regions[i].h;
            local[i].inverse = regions[i].inverse;
            local[i].index = regions[i].index;
        }
        passed = local;
    }
    return plt_venc_set_osd(enc->enc, passed, count);
}
