/**
 * @file    pm_image.h
 * @brief   各层共用的状态码和图像。图像整块传递，色彩范围跟图像走，不在过一层时丢掉。
 */
#ifndef PM_IMAGE_H
#define PM_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    PM_OK = 0,
    PM_ERR = -1,
    /* 这次没有数据，或这一包没有被收下。稍后再试。 */
    PM_AGAIN = -11,
    /* 码流或图像格式这颗芯片不支持，例如 10bit。调用方不要按同一参数反复重建。 */
    PM_UNSUPPORTED = -95
}
PmStatus;

typedef enum
{
    PM_PIX_NV12 = 0,
    PM_PIX_RGB888 = 1
}
PmPixelFormat;

typedef enum
{
    /* 视频常见的有限范围。只对 YUV 有意义。 */
    PM_RANGE_LIMITED = 0,
    /* JPEG 和按全范围转出来的 YUV。 */
    PM_RANGE_FULL = 1
}
PmColorRange;

typedef enum
{
    PM_CODEC_H264 = 0,
    PM_CODEC_H265 = 1
}
PmCodec;

typedef struct PmRect
{
    int x;
    int y;
    int w;
    int h;
}
PmRect;

/*
 * 一块设备可见的图像。fd 是 dmabuf，别的平台可以没有，所以有没有图像只看 handle。
 * 宽高是有效尺寸，hor_stride / ver_stride 是存储跨度，单位是像素。
 * refcount 先留给以后的共享所有权。这一轮解码帧只由 plt_vdec_release 归还，
 * plt_mem_alloc 出来的缓冲只由 plt_mem_free 释放，不要混用。
 */
typedef struct PmImage
{
    int format;
    int range;
    int width;
    int height;
    int hor_stride;
    int ver_stride;
    int fd;
    void *ptr;
    int size;
    void *handle;
    int refcount;
}
PmImage;

static inline int pm_image_valid(const PmImage *image)
{
    return image != NULL && image->handle != NULL;
}

#ifdef __cplusplus
}
#endif

#endif
