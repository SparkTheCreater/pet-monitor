/**
 * @file    pm_channel.h
 * @brief   一路视频：包队列、解码、按订阅留最新帧、在副本上截图。
 *          这一层承接原来 sys 里和通道相关的封装，不包含画面合成和视频编码。
 */
#ifndef PM_CHANNEL_H
#define PM_CHANNEL_H

#include "pm_packet.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct PmChannel PmChannel;

typedef struct PmChannelConfig
{
    /* 写进帧里的通道号，也用来给解码线程起名。 */
    int channel;
    /* PM_CODEC_H264 或 PM_CODEC_H265。 */
    int codec;
    /* 解码器外部缓冲块数。参考帧再加上每个订阅者两块。 */
    int buf_count;
    /* 包队列上限。满了就丢到下一个关键帧，不拆收流连接。 */
    int queue_limit;
    /* 参数集。可以是 Annex B，也可以是 H.264 的 AVCC。没有就传空。 */
    const uint8_t *extradata;
    size_t extradata_size;
}
PmChannelConfig;

/* 同步。打开解码器并起解码线程。失败时 *out 为空。调用前要先 plt_init。 */
PmStatus pm_channel_open(const PmChannelConfig *cfg, PmChannel **out);

/* 同步。停解码线程，归还还借着的帧，关掉解码器。 */
void pm_channel_close(PmChannel *channel);

/*
 * 收流线程调用。数据会被拷走。
 * 队列满时丢掉这一包以及后面的非关键帧，直到下一个关键帧，仍然返回 PM_OK。
 * 解码器报告格式不支持时返回 PM_UNSUPPORTED。
 */
PmStatus pm_channel_push(PmChannel *channel, const PmPacketView *packet);

/* 从打开到现在因为队列满而丢掉的包数。 */
int pm_channel_dropped(const PmChannel *channel);

/*
 * 订阅解码帧。fps 大于 0 时按这个帧率抽帧，0 表示每帧都要。
 * 成功返回不小于 0 的订阅号。槽里只留最新的一帧，新帧覆盖还没取走的旧帧。
 * sink 在解码线程执行，可以为空；为空时自己用 acquire 取。
 */
int pm_channel_subscribe(PmChannel *channel, double fps, PmFrameSink sink, void *user);

/* 同步。退订并归还这一路还借着的帧。 */
PmStatus pm_channel_unsubscribe(PmChannel *channel, int sub);

/*
 * 取走槽里的最新帧。没有帧返回 PM_AGAIN。
 * 上一帧还没 release 时返回 PM_ERR。取到的帧在 release 之前有效。
 */
PmStatus pm_channel_acquire(PmChannel *channel, int sub, PmVideoFrame *frame);

/* 同步。把 acquire 取走的帧还回去。还没取走过也返回 PM_OK。 */
PmStatus pm_channel_release(PmChannel *channel, int sub);

/*
 * 同步。把 frame 拷到一块新缓冲上，画框，再编成 JPEG。不改原来的解码帧。
 * 线宽 2 像素，JPEG 质量 80。成功时 *jpeg 用 plt_jpeg_free 释放。
 */
PmStatus pm_channel_snapshot(PmChannel *channel,
                             const PmVideoFrame *frame,
                             const PmRect *boxes,
                             int box_count,
                             uint8_t **jpeg,
                             size_t *jpeg_len);

#ifdef __cplusplus
}
#endif

#endif
