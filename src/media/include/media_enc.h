/**
 * @file    media_enc.h
 * @brief   视频编码。参数由调用方给全，这里不填默认值。
 */
#ifndef MEDIA_ENC_H
#define MEDIA_ENC_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct MediaEnc MediaEnc;

typedef enum
{
    MEDIA_RC_CBR = 0,
    MEDIA_RC_VBR = 1
}
MediaRateControl;

/* 每个字段都要合法。不合法时 open 失败，不会改用另一套参数。 */
typedef struct MediaEncParam
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
MediaEncParam;

typedef struct MediaEncPacket
{
    uint8_t *data;
    size_t size;
    int64_t pts_us;
    int key;
}
MediaEncPacket;

/*
 * 同步。参数不合法返回 BASE_ERR。
 * 平台层还没有视频编码器，参数合法时返回 BASE_UNSUPPORTED，*out 为空。
 * JPEG 用 media_jpeg_encode。
 */
BaseStatus media_enc_open(const MediaEncParam *param, MediaEnc **out);

void media_enc_close(MediaEnc *enc);

/* 同步。格式、宽高、行宽要和 open 时一致。返回时这一帧的码流已经取出。 */
BaseStatus media_enc_send(MediaEnc *enc, const BaseImage *image, int64_t pts_us);

/*
 * 非阻塞。没有包返回 BASE_AGAIN。
 * 成功时 *packet 的 data 由本层分配，用 media_enc_packet_free 释放。
 */
BaseStatus media_enc_receive(MediaEnc *enc, MediaEncPacket *packet);

void media_enc_packet_free(MediaEncPacket *packet);

/* 下一帧编成关键帧。和 send 由调用方串行调用。 */
BaseStatus media_enc_request_keyframe(MediaEnc *enc);

/* 下一帧起用新的目标码率。码率模式和 QP 不变。 */
BaseStatus media_enc_set_bitrate(MediaEnc *enc, int bitrate);

#ifdef __cplusplus
}
#endif

#endif
