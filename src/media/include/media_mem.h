/**
 * @file    media_mem.h
 * @brief   设备可见的图像缓冲。图形、解码、编码和推理都用这块缓冲。
 */
#ifndef MEDIA_MEM_H
#define MEDIA_MEM_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * 同步。申请一块图像，行宽和行数按 16 对齐。width、height 仍是有效尺寸。
 * NV12 的 range 初始化为有限范围；要全范围时由调用方在转换之后写 image->range。
 * 成功后缓冲归调用方，用 media_mem_free 释放。media_init 之前调用失败。
 * 不要拿来释放解码帧，解码帧用 media_dec_release。
 */
BaseStatus media_mem_alloc(int width, int height, int format, BaseImage *out);

/* 同步。释放后把 image 清成无效。空指针或已经无效的缓冲直接返回。 */
void media_mem_free(BaseImage *image);

/*
 * 同步。CPU 直接写过映射内存后，把 cache 刷给设备。
 * 设备读这块缓冲时，不刷就可能读到旧数据。
 */
BaseStatus media_mem_flush(BaseImage *image);

#ifdef __cplusplus
}
#endif

#endif
