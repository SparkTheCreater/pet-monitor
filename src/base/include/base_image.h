/**
 * @file    base_image.h
 * @brief   各层共用的状态码和图像。图像整块传递，色彩范围跟图像走，不在过一层时丢掉。
 */
#ifndef BASE_IMAGE_H
#define BASE_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    BASE_OK = 0,
    BASE_ERR = -1,
    /* 这次没有数据，或这一包没有被收下。稍后再试。 */
    BASE_AGAIN = -11,
    /* 码流或图像格式这颗芯片不支持，例如 10bit。调用方不要按同一参数反复重建。 */
    BASE_UNSUPPORTED = -95
}
BaseStatus;

typedef enum
{
    BASE_PIX_NV12 = 0,
    BASE_PIX_RGB888 = 1
}
BasePixelFormat;

typedef enum
{
    /* 视频常见的有限范围。只对 YUV 有意义。 */
    BASE_RANGE_LIMITED = 0,
    /* JPEG 和按全范围转出来的 YUV。 */
    BASE_RANGE_FULL = 1
}
BaseColorRange;

typedef enum
{
    BASE_CODEC_H264 = 0,
    BASE_CODEC_H265 = 1
}
BaseCodec;

typedef struct BaseRect
{
    int x;
    int y;
    int w;
    int h;
}
BaseRect;

/*
 * 一块设备可见的图像。fd 是 dmabuf，别的平台可以没有，所以有没有图像只看 handle。
 * 宽高是有效尺寸，hor_stride / ver_stride 是存储跨度，单位是像素。
 * refcount 先留给以后的共享所有权。这一轮解码帧只由 plt_vdec_release 归还，
 * plt_mem_alloc 出来的缓冲只由 plt_mem_free 释放，不要混用。
 */
typedef struct BaseImage
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
BaseImage;

/* 解码器吐出的一帧。有图像的帧和只有 eos 的空帧都要归还。 */
typedef struct BaseDecodedFrame
{
    BaseImage image;
    int64_t pts_us;
    int eos;
    void *owner;
}
BaseDecodedFrame;

static inline int base_image_valid(const BaseImage *image)
{
    return image != NULL && image->handle != NULL;
}

#ifdef __cplusplus
}
#endif

#endif
