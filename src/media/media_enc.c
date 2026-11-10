/**
 * @file    media_enc.c
 * @brief   视频编码接口。平台层还没有视频编码器，打开时明确返回不支持。
 */
#include "media_enc.h"

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
    if (param->codec == BASE_CODEC_H264 && (param->profile < 0 || param->level <= 0))
    {
        return 0;
    }
    return 1;
}

BaseStatus media_enc_open(const MediaEncParam *param, MediaEnc **out)
{
    if (out != NULL)
    {
        *out = NULL;
    }
    if (param == NULL || out == NULL || !ParamOk(param))
    {
        return BASE_ERR;
    }
    return BASE_UNSUPPORTED;
}

void media_enc_close(MediaEnc *encoder)
{
    (void)encoder;
}

BaseStatus media_enc_send(MediaEnc *encoder, const BaseImage *image, int64_t pts_us)
{
    (void)encoder;
    (void)image;
    (void)pts_us;
    return BASE_ERR;
}

BaseStatus media_enc_receive(MediaEnc *encoder, MediaEncPacket *packet)
{
    (void)encoder;
    if (packet != NULL)
    {
        packet->data = NULL;
        packet->size = 0;
        packet->pts_us = 0;
        packet->key = 0;
    }
    return BASE_ERR;
}

void media_enc_packet_free(MediaEncPacket *packet)
{
    (void)packet;
}

BaseStatus media_enc_request_keyframe(MediaEnc *encoder)
{
    (void)encoder;
    return BASE_ERR;
}

BaseStatus media_enc_set_bitrate(MediaEnc *encoder, int bitrate)
{
    (void)encoder;
    (void)bitrate;
    return BASE_ERR;
}
