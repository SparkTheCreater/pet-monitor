/**
 * @file    app_mark.h
 * @brief   拷一份 NV12，用 RGA 画猫框，并用 RGA 填色把置信度叠在框旁。
 *
 * 不改 src。成功时 *out 归调用方，用 media_mem_free 释放。
 */
#ifndef APP_MARK_H
#define APP_MARK_H

#include "algo.h"

#ifdef __cplusplus
extern "C"
{
#endif

BaseStatus app_mark_cats(const BaseImage *src, const AlgoDetectResult *result, BaseImage *out);

#ifdef __cplusplus
}
#endif

#endif
