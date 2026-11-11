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
 * 同步。参数不合法返回 BASE_ERR，*out 为空。
 * 这颗平台没有视频编码器时返回 BASE_UNSUPPORTED，*out 为空。
 * JPEG 用 media_jpeg_encode。
 */
BaseStatus media_enc_open(const MediaEncParam *param, MediaEnc **out);

void media_enc_close(MediaEnc *enc);

/*
 * 取 SPS/PPS，H.265 还带 VPS。data 指向编码器内部缓存，close 之前一直有效。
 * 还没生成或编码器没打开时返回 BASE_ERR，*data 为空。
 */
BaseStatus media_enc_extradata(MediaEnc *enc, const uint8_t **data, size_t *size);

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

/*
 * 下一帧起逆时针旋转。angle 为 0、90、180、270。
 * 送进来的图宽高不变。90 和 270 时码流宽高对调，参数集会更新。
 * 不合法时保持原角度。和 send 由调用方串行调用。
 */
BaseStatus media_enc_set_rotation(MediaEnc *enc, int angle);

/*
 * 下一帧起镜像。direction 为 0 不镜像、1 水平、2 垂直。
 * 不合法时保持原镜像。和 send 由调用方串行调用。
 */
BaseStatus media_enc_set_mirror(MediaEnc *enc, int direction);

/* 一块感兴趣区域。坐标和宽高按像素，必须 16 对齐，并且落在画面里。 */
typedef struct MediaEncRoi
{
    int x;
    int y;
    int w;
    int h;
    /* 1 用绝对 QP，0 在当前 QP 上加减。 */
    int abs_qp;
    /* 绝对时 0–51。相对时 -51–51。 */
    int qp;
    /* 1 这块强制帧内编码。 */
    int intra;
}
MediaEncRoi;

/*
 * 设置下一帧起的感兴趣区域。最多 8 块，count 为 0 时清掉。
 * 和 send 由调用方串行调用。不合法时保持原配置。
 * 编码器没打开时返回 BASE_ERR。
 */
BaseStatus media_enc_set_roi(MediaEnc *enc, const MediaEncRoi *regions, int count);

/* 调色板里的一色。下标图里的每个字节选这里的一项。 */
typedef struct MediaEncOsdColor
{
    uint8_t y;
    uint8_t u;
    uint8_t v;
    uint8_t alpha;
}
MediaEncOsdColor;

/*
 * 设置调色板，最多 256 色。没给出的下标清成 0。
 * count 为 0 时改回硬件默认调色板。不合法时保持原配置。
 * 编码器没打开时返回 BASE_ERR。
 */
BaseStatus media_enc_set_osd_palette(MediaEnc *enc, const MediaEncOsdColor *colors, int count);

/* 一块 OSD。坐标和宽高按像素，必须 16 对齐，并且落在画面里。 */
typedef struct MediaEncOsd
{
    int x;
    int y;
    int w;
    int h;
    /* 1 按背景亮度反色。 */
    int inverse;
    /* 每像素 1 字节调色板下标，长度 w * h，行优先。这里不接收文字。 */
    const uint8_t *index;
}
MediaEncOsd;

/*
 * 设置下一帧起的 OSD。最多 8 块，count 为 0 时清掉。
 * 返回前已经把下标拷走。不合法时保持原配置。
 * 和 send 由调用方串行调用。编码器没打开时返回 BASE_ERR。
 */
BaseStatus media_enc_set_osd(MediaEnc *enc, const MediaEncOsd *regions, int count);

#ifdef __cplusplus
}
#endif

#endif
