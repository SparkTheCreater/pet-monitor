/**
 * @file    pm_packet.h
 * @brief   媒体层进出的码流包和视频帧。图像整块带走，包含色彩范围。
 */
#ifndef PM_PACKET_H
#define PM_PACKET_H

#include "pm_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 调用方持有的一包码流。push 返回后就可以释放 data。 */
typedef struct PmPacketView
{
    int key;
    int64_t pts_us;
    int64_t recv_us;
    const uint8_t *data;
    size_t size;
}
PmPacketView;

/*
 * 借出的一帧。image 在 pm_channel_release 之前有效。
 * 这是解码器的缓冲，不要交给 plt_mem_free 或 plt_vdec_release。
 */
typedef struct PmVideoFrame
{
    int channel;
    uint64_t seq;
    int64_t pts_us;
    int64_t recv_us;
    PmImage image;
}
PmVideoFrame;

/* 在解码线程里执行。只许把通知放进自己的队列，不许在这里推理或发信。 */
typedef void (*PmFrameSink)(const PmVideoFrame *frame, void *user);

#ifdef __cplusplus
}
#endif

#endif
