/**
 * @file    media_jpeg.h
 * @brief   把一帧编成 JPEG。返回时编码已经做完。
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

#ifdef __cplusplus
}
#endif

#endif
