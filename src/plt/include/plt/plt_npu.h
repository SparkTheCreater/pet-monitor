/**
 * @file    plt_npu.h
 * @brief   加载模型并跑一次推理。不做检测后处理。
 */
#ifndef PLT_NPU_H
#define PLT_NPU_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct PltNpu PltNpu;

typedef enum
{
    PLT_NPU_FLOAT32 = 0,
    PLT_NPU_FLOAT16,
    PLT_NPU_INT8,
    PLT_NPU_UINT8,
    PLT_NPU_INT16,
    PLT_NPU_UINT16,
    PLT_NPU_INT32,
    PLT_NPU_UINT32,
    PLT_NPU_INT64,
    PLT_NPU_TYPE_UNKNOWN
}
PltNpuType;

typedef enum
{
    PLT_NPU_QNT_NONE = 0,
    PLT_NPU_QNT_DFP,
    PLT_NPU_QNT_AFFINE,
    PLT_NPU_QNT_UNKNOWN
}
PltNpuQuant;

/* dims 的排列。取 PltNpuFmt。 */
typedef enum
{
    PLT_NPU_FMT_NCHW = 0,
    PLT_NPU_FMT_NHWC,
    PLT_NPU_FMT_NC1HWC2,
    PLT_NPU_FMT_UNKNOWN
}
PltNpuFmt;

/*
 * 一个输入或输出张量。type 取 PltNpuType，fmt 取 PltNpuFmt，qntType 取 PltNpuQuant。
 * 仿射量化用 zp 和 scale 还原：value = (q - zp) * scale。
 * 动态定点用 fl 还原：value = q * 2^(-fl)。
 * 调用方按 bytes 和 fmt 使用缓冲，不要从 type 自行推断字节布局。
 */
typedef struct PltNpuTensor
{
    /* 张量序号。输入固定是 0。 */
    int index;
    /* dims 里有效的维数。 */
    int nDims;
    /* 每一维的长度，只用前 nDims 项。 */
    int dims[16];
    /* PltNpuType。 */
    int type;
    /* PltNpuFmt。dims 按这个顺序排。 */
    int fmt;
    /* PltNpuQuant。 */
    int qntType;
    /* 动态定点的小数位。其它量化方式不用。 */
    int fl;
    /* 仿射量化的零点。其它量化方式不用。 */
    int zp;
    /* 仿射量化的系数。其它量化方式不用。 */
    float scale;
    /* 元素个数。 */
    uint32_t nElems;
    /* 缓冲字节数。调用方按这个准备输出。 */
    uint32_t bytes;
}
PltNpuTensor;

/* 同步。从文件加载模型。只接受单输入模型。失败时 *out 为空。 */
BaseStatus plt_npu_open(const char* modelPath, PltNpu** out);

/* 同步。卸载模型。空指针直接返回。 */
void plt_npu_close(PltNpu* npu);

/*
 * 同步。把以后的推理绑到指定核。0 表示自动，其余取值由当前平台解释。
 * 没有这个能力时返回 BASE_ERR。
 */
BaseStatus plt_npu_set_core(PltNpu* npu, int coreMask);

/* 同步。读模型输入的形状。 */
BaseStatus plt_npu_input_info(PltNpu* npu, PltNpuTensor* info);

/* 同步。写出输出个数。 */
BaseStatus plt_npu_output_count(PltNpu* npu, int* count);

/* 同步。读第 index 个输出的形状。 */
BaseStatus plt_npu_output_info(PltNpu* npu, int index, PltNpuTensor* info);

/*
 * 同步。input 是 RGB888，宽高与模型输入一致。返回之前不得改写 input。
 * 行宽为 0 或等于宽度时直接用 fd，并先把 CPU cache 刷给设备。
 * 行宽更大时，CPU 先读到最新像素，再按行收成紧密 RGB888 送入。
 * outputs[i] 由调用方提供，bytes[i] 不得小于该输出的 bytes。
 * 原始输出按 fmt 拷进调用方的缓冲，不在这里做阈值和画框。
 */
BaseStatus plt_npu_run(PltNpu* npu,
                     const BaseImage* input,
                     void* const* outputs,
                     const size_t* bytes);

#ifdef __cplusplus
}
#endif

#endif
