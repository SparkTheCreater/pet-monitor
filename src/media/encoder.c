/**
 * @file    encoder.c
 * @brief   视频编码接口。平台层还没有视频编码器，打开时明确返回不支持。
 */
#include "media/pm_encoder.h"

static int ParamOk(const PmEncoderParam *param)
{
    if (param->codec != PM_CODEC_H264 && param->codec != PM_CODEC_H265)
    {
        return 0;
    }
    if (param->format != PM_PIX_NV12 && param->format != PM_PIX_RGB888)
    {
        return 0;
    }
    if (param->width <= 0 || param->height <= 0 || param->fps <= 0 || param->gop <= 0 || param->bitrate <= 0)
    {
        return 0;
    }
    if (param->rc_mode != PM_RC_CBR && param->rc_mode != PM_RC_VBR)
    {
        return 0;
    }
    if (param->qp_min < 0 || param->qp_max > 51 || param->qp_min > param->qp_init || param->qp_init > param->qp_max)
    {
        return 0;
    }
    if (param->codec == PM_CODEC_H264 && (param->profile < 0 || param->level <= 0))
    {
        return 0;
    }
    return 1;
}

PmStatus pm_encoder_open(const PmEncoderParam *param, PmEncoder **out)
{
    if (out != NULL)
    {
        *out = NULL;
    }
    if (param == NULL || out == NULL || !ParamOk(param))
    {
        return PM_ERR;
    }
    return PM_UNSUPPORTED;
}

void pm_encoder_close(PmEncoder *encoder)
{
    (void)encoder;
}

PmStatus pm_encoder_send(PmEncoder *encoder, const PmImage *image, int64_t pts_us)
{
    (void)encoder;
    (void)image;
    (void)pts_us;
    return PM_ERR;
}

PmStatus pm_encoder_receive(PmEncoder *encoder, PmEncodedPacket *packet)
{
    (void)encoder;
    if (packet != NULL)
    {
        packet->data = NULL;
        packet->size = 0;
        packet->pts_us = 0;
        packet->key = 0;
    }
    return PM_ERR;
}

void pm_encoder_packet_free(PmEncodedPacket *packet)
{
    (void)packet;
}

PmStatus pm_encoder_request_keyframe(PmEncoder *encoder)
{
    (void)encoder;
    return PM_ERR;
}

PmStatus pm_encoder_set_bitrate(PmEncoder *encoder, int bitrate)
{
    (void)encoder;
    (void)bitrate;
    return PM_ERR;
}
