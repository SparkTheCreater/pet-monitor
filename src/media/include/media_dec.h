/**
 * @file    media_dec.h
 * @brief   视频解码。一包一帧的 Annex B，输出帧用完要归还。
 */
#ifndef MEDIA_DEC_H
#define MEDIA_DEC_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct MediaDec MediaDec;

/*
 * 同步。codec 是 H.264 或 H.265，buf_count 是解码缓冲块数。
 * extradata 可以是 Annex B，也可以是 H.264 的 AVCC。没有就传空。
 * 失败时 *out 为空。调用前要先 media_init。
 */
BaseStatus media_dec_open(int codec,
                          int buf_count,
                          const uint8_t *extradata,
                          size_t extradata_size,
                          MediaDec **out);

/* 同步。停解码器。还没归还的帧一起失效。 */
void media_dec_close(MediaDec *dec);

/*
 * 非阻塞。第一包之前会先送参数集。
 * 返回 BASE_OK 时这一包已经送进解码器。返回 BASE_AGAIN 时这一包没被收下，原包重送。
 * eos 为 1 表示码流结束，data 可以为空。
 */
BaseStatus media_dec_send(MediaDec *dec, const uint8_t *data, int len, int64_t pts_us, int eos);

/*
 * 非阻塞。没有帧返回 BASE_AGAIN。
 * 图像帧带色彩范围。图像帧和只有 eos 的空帧都要 media_dec_release。
 * 不是 8bit NV12 时返回 BASE_UNSUPPORTED。
 */
BaseStatus media_dec_receive(MediaDec *dec, BaseDecodedFrame *frame);

/* 同步。归还 receive 拿到的帧。返回之后这块缓冲可以给下一帧用。 */
BaseStatus media_dec_release(MediaDec *dec, BaseDecodedFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
