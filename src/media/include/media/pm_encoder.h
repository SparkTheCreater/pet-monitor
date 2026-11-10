/**
 * @file    pm_encoder.h
 * @brief   视频编码。参数由调用方给全，这里不填默认值。
 */
#ifndef PM_ENCODER_H
#define PM_ENCODER_H

#include "pm_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct PmEncoder PmEncoder;

typedef enum
{
    PM_RC_CBR = 0,
    PM_RC_VBR = 1
}
PmRateControl;

/* 每个字段都要合法。不合法时 open 失败，不会改用另一套参数。 */
typedef struct PmEncoderParam
{
    int codec;
    int format;
    int width;
    int height;
    int fps;
    int gop;
    int rc_mode;
    int bitrate;
    int qp_init;
    int qp_min;
    int qp_max;
    /* 只对 H.264：66 Baseline、77 Main、100 High。level 40 即 4.0。 */
    int profile;
    int level;
}
PmEncoderParam;

typedef struct PmEncodedPacket
{
    uint8_t *data;
    size_t size;
    int64_t pts_us;
    int key;
}
PmEncodedPacket;

/*
 * 同步。参数不合法返回 PM_ERR。
 * 平台层还没有视频编码器，参数合法时返回 PM_UNSUPPORTED，*out 为空。
 * JPEG 仍用 plt_jpeg_encode。
 */
PmStatus pm_encoder_open(const PmEncoderParam *param, PmEncoder **out);

void pm_encoder_close(PmEncoder *encoder);

/* 同步。格式、宽高、行宽要和 open 时一致。返回时这一帧的码流已经取出。 */
PmStatus pm_encoder_send(PmEncoder *encoder, const PmImage *image, int64_t pts_us);

/*
 * 非阻塞。没有包返回 PM_AGAIN。
 * 成功时 *packet 的 data 由本层分配，用 pm_encoder_packet_free 释放。
 */
PmStatus pm_encoder_receive(PmEncoder *encoder, PmEncodedPacket *packet);

void pm_encoder_packet_free(PmEncodedPacket *packet);

/* 下一帧编成关键帧。和 send 由调用方串行调用。 */
PmStatus pm_encoder_request_keyframe(PmEncoder *encoder);

/* 下一帧起用新的目标码率。码率模式和 QP 不变。 */
PmStatus pm_encoder_set_bitrate(PmEncoder *encoder, int bitrate);

#ifdef __cplusplus
}
#endif

#endif
