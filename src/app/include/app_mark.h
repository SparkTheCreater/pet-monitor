/**
 * @file    app_mark.h
 * @brief   拷一份 NV12，再按调用方的选择画框、叠置信度。
 *
 * 不改 src。app_mark_copy 成功时 *out 归调用方，用 media_mem_free 释放。
 * 画框和叠置信度分开调用，调用方按配置决定要不要做。
 */
#ifndef APP_MARK_H
#define APP_MARK_H

#include "algo.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 拷一份 NV12，不画。 */
BaseStatus app_mark_copy(const BaseImage *src, BaseImage *out);

/* 在已有的 NV12 上画框。没有框时直接成功。 */
BaseStatus app_mark_boxes(BaseImage *image, const AlgoDetectResult *result);

/* 在已有的 NV12 上把置信度叠在框旁。没有框时直接成功。 */
BaseStatus app_mark_scores(BaseImage *image, const AlgoDetectResult *result);

/* 拷贝、画框、叠置信度都做。 */
BaseStatus app_mark_cats(const BaseImage *src, const AlgoDetectResult *result, BaseImage *out);

#ifdef __cplusplus
}
#endif

#endif
