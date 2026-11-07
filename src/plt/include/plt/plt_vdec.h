/**
 * @file    plt_vdec.h
 * @brief   视频解码。一包一帧的 Annex B。输出帧在 release 之前有效。
 */
#ifndef PM_PLT_VDEC_H
#define PM_PLT_VDEC_H

#include "pm_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct PltVdec PltVdec;

/*
 * 解码器吐出的一帧。有图像的帧和只有 eos 的空帧都要 release。
 * image 在 release 之前有效。pts_us 是送进来的时间戳，原样带回。
 */
typedef struct PmDecodedFrame
{
    PmImage image;
    int64_t pts_us;
    int eos;
    void *owner;
}
PmDecodedFrame;

/*
 * 同步建解码器，此时还不建缓冲池。buf_count 是外部缓冲块数，由调用方按
 * 参考帧需求和订阅者数量算好。codec 只能是 H.264 或 H.265。
 * 输入按 Annex B，一包一帧。plt_init 之前失败。
 */
PmStatus plt_vdec_open(int codec, int buf_count, PltVdec **out);

/* 同步。先停解码器再释放缓冲。还没 release 的帧一起失效。 */
void plt_vdec_close(PltVdec *dec);

/*
 * 非阻塞。返回 PM_OK 时码流已经拷进解码器，调用方可以释放 data。
 * 返回 PM_AGAIN 表示这一包没有被收下，先取帧再原包重送。
 * eos 为 1 表示码流结束，data 可以为空。
 */
PmStatus plt_vdec_send(PltVdec *dec, const uint8_t *data, int len, int64_t pts_us, int eos);

/*
 * 非阻塞。没有帧返回 PM_AGAIN。
 * PM_OK 时有两种帧：pm_image_valid 为真是 8bit NV12；只有 eos 的空帧没有图像。
 * 两种都要 release。range 取自解码器报告的色彩范围，未标明时按有限范围。
 * 输出不是 8bit NV12 时返回 PM_UNSUPPORTED，这一帧不会交给调用方。
 * 分辨率变化在内部换缓冲组，不交给调用方。
 */
PmStatus plt_vdec_get(PltVdec *dec, PmDecodedFrame *frame);

/* 同步。归还 get 拿到的帧。返回之后像素可以被下一帧覆盖。 */
PmStatus plt_vdec_release(PltVdec *dec, PmDecodedFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
