/**
 * @file    plt_jpeg.h
 * @brief   整张 JPEG 的解码和编码。返回时硬件已经做完。
 */
#ifndef PLT_JPEG_H
#define PLT_JPEG_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * 同步。image 必须是 NV12。quality 由调用方给出，范围 1 到 100，这里不填默认值。
 * 有限范围先换成全范围再编码，避免画面发灰。全范围的图像直接编码。
 * 成功时 *jpeg 由 plt 分配，调用方用 plt_jpeg_free 交还。失败时 *jpeg 为空，不用释放。
 * 返回前不修改调用方的 image。
 */
BaseStatus plt_jpeg_encode(const BaseImage *image,
                         int quality,
                         uint8_t **jpeg,
                         size_t *jpeg_len);

/* 释放 plt_jpeg_encode 交出来的缓冲。空指针直接返回。 */
void plt_jpeg_free(uint8_t *jpeg);

/*
 * 同步。把一整张 baseline JPEG 解成 NV12，色彩范围是全范围。
 * 宽高从 SOF 读出，缓冲按 16 对齐，和 plt_mem_alloc 相同。
 * 成功时 *image 归调用方，用 plt_mem_free 释放。失败时 *image 无效，不用释放。
 * 渐进式等芯片解不了的图返回 BASE_ERR。plt_init 之前失败。
 */
BaseStatus plt_jpeg_decode(const uint8_t *data, size_t size, BaseImage *image);

#ifdef __cplusplus
}
#endif

#endif
