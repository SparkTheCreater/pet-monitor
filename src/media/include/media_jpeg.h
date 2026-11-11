/**
 * @file    media_jpeg.h
 * @brief   整张 JPEG 的解码和编码。返回时硬件已经做完。
 */
#ifndef MEDIA_JPEG_H
#define MEDIA_JPEG_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * 同步。image 必须是 NV12。quality 由调用方给出，范围 1 到 100。
 * 成功时 *jpeg 由本层分配，调用方用 media_jpeg_free 交还。失败时 *jpeg 为空。
 * 返回前不修改调用方的 image。
 */
BaseStatus media_jpeg_encode(const BaseImage *image, int quality, uint8_t **jpeg, size_t *jpeg_len);

/* 释放 media_jpeg_encode 交出来的缓冲。空指针直接返回。 */
void media_jpeg_free(uint8_t *jpeg);

/*
 * 同步。把一整张 baseline JPEG 解成 NV12，色彩范围是全范围。
 * 成功时 *image 由本层申请，调用方用 plt_mem_free 释放。失败时 *image 无效，不用释放。
 * 渐进式等芯片解不了的图返回 BASE_ERR。
 */
BaseStatus media_jpeg_decode(const uint8_t *data, size_t size, BaseImage *image);

#ifdef __cplusplus
}
#endif

#endif
