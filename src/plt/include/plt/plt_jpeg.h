/**
 * @file    plt_jpeg.h
 * @brief   把 NV12 编成 JPEG。返回时编码已经做完。
 */
#ifndef PM_PLT_JPEG_H
#define PM_PLT_JPEG_H

#include "pm_image.h"

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
PmStatus plt_jpeg_encode(const PmImage *image,
                         int quality,
                         uint8_t **jpeg,
                         size_t *jpeg_len);

/* 释放 plt_jpeg_encode 交出来的缓冲。空指针直接返回。 */
void plt_jpeg_free(uint8_t *jpeg);

#ifdef __cplusplus
}
#endif

#endif
