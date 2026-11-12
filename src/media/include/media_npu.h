/**
 * @file    media_npu.h
 * @brief   加载模型并跑一次推理。不做检测后处理。
 */
#ifndef MEDIA_NPU_H
#define MEDIA_NPU_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct MediaNpu MediaNpu;

typedef enum
{
    MEDIA_NPU_FLOAT32 = 0,
    MEDIA_NPU_FLOAT16,
    MEDIA_NPU_INT8,
    MEDIA_NPU_UINT8,
    MEDIA_NPU_INT16,
    MEDIA_NPU_UINT16,
    MEDIA_NPU_INT32,
    MEDIA_NPU_UINT32,
    MEDIA_NPU_INT64,
    MEDIA_NPU_TYPE_UNKNOWN
}
MediaNpuType;

typedef enum
{
    MEDIA_NPU_QNT_NONE = 0,
    MEDIA_NPU_QNT_DFP,
    MEDIA_NPU_QNT_AFFINE,
    MEDIA_NPU_QNT_UNKNOWN
}
MediaNpuQuant;

/* dims 的排列。取 MediaNpuFmt。 */
typedef enum
{
    MEDIA_NPU_FMT_NCHW = 0,
    MEDIA_NPU_FMT_NHWC,
    MEDIA_NPU_FMT_NC1HWC2,
    MEDIA_NPU_FMT_UNKNOWN
}
MediaNpuFmt;

/*
 * 一个输入或输出张量。type 取 MediaNpuType，fmt 取 MediaNpuFmt，qnt_type 取 MediaNpuQuant。
 * 仿射量化用 zp 和 scale 还原：value = (q - zp) * scale。
 * 动态定点用 fl 还原：value = q * 2^(-fl)。
 * 调用方按 bytes 和 fmt 使用缓冲，不要从 type 自行推断字节布局。
 */
typedef struct MediaNpuTensor
{
    int index;
    int n_dims;
    int dims[16];
    int type;
    int fmt;
    int qnt_type;
    int fl;
    int zp;
    float scale;
    uint32_t n_elems;
    uint32_t bytes;
}
MediaNpuTensor;

/* 同步。从文件加载模型。只接受单输入模型。失败时 *out 为空。 */
BaseStatus media_npu_open(const char *model_path, MediaNpu **out);

/* 同步。卸载模型。空指针直接返回。 */
void media_npu_close(MediaNpu *npu);

/*
 * 同步。把以后的推理绑到指定核。0 表示自动，其余取值由当前平台解释。
 * 没有这个能力时返回 BASE_ERR。
 */
BaseStatus media_npu_set_core(MediaNpu *npu, int core_mask);

/* 同步。读模型输入的形状。 */
BaseStatus media_npu_input_info(MediaNpu *npu, MediaNpuTensor *info);

/* 同步。写出输出个数。 */
BaseStatus media_npu_output_count(MediaNpu *npu, int *count);

/* 同步。读第 index 个输出的形状。 */
BaseStatus media_npu_output_info(MediaNpu *npu, int index, MediaNpuTensor *info);

/*
 * 同步。input 是 RGB888，宽高与模型输入一致。返回之前不得改写 input。
 * 行宽为 0 或等于宽度时直接用 fd，并先把 CPU cache 刷给设备。
 * 行宽更大时，CPU 先读到最新像素，再按行收成紧密 RGB888 送入。
 * outputs[i] 由调用方提供，bytes[i] 不得小于该输出的 bytes。
 * 原始输出按 fmt 拷进调用方的缓冲，不在这里做阈值和画框。
 */
BaseStatus media_npu_run(MediaNpu *npu,
                       const BaseImage *input,
                       void *const *outputs,
                       const size_t *bytes);

#ifdef __cplusplus
}
#endif

#endif
