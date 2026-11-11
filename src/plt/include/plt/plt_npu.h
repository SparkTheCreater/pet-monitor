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

/*
 * 一个输入或输出张量。type 取 PltNpuType，qnt_type 取 PltNpuQuant。
 * 仿射量化用 zp 和 scale 还原：value = (q - zp) * scale。
 * 调用方按 bytes 准备缓冲，不要从 type 自行推断字节布局。
 */
typedef struct PltNpuTensor
{
    int index;
    int n_dims;
    int dims[16];
    int type;
    int qnt_type;
    int zp;
    float scale;
    uint32_t n_elems;
    uint32_t bytes;
}
PltNpuTensor;

/* 同步。从文件加载模型。只接受单输入模型。失败时 *out 为空。 */
BaseStatus plt_npu_open(const char *model_path, PltNpu **out);

/* 同步。卸载模型。空指针直接返回。 */
void plt_npu_close(PltNpu *npu);

/*
 * 同步。把以后的推理绑到指定核。0 表示自动，其余取值由当前平台解释。
 * 没有这个能力时返回 BASE_ERR。
 */
BaseStatus plt_npu_set_core(PltNpu *npu, int core_mask);

/* 同步。读模型输入的形状。 */
BaseStatus plt_npu_input_info(PltNpu *npu, PltNpuTensor *info);

/* 同步。写出输出个数。 */
BaseStatus plt_npu_output_count(PltNpu *npu, int *count);

/* 同步。读第 index 个输出的形状。 */
BaseStatus plt_npu_output_info(PltNpu *npu, int index, PltNpuTensor *info);

/*
 * 同步。input 是 RGB888，宽高与模型输入一致。返回之前不得改写 input。
 * outputs[i] 由调用方提供，bytes[i] 不得小于该输出的 bytes。
 * 原始输出拷进调用方的缓冲，不在这里做阈值和画框。
 */
BaseStatus plt_npu_run(PltNpu *npu,
                     const BaseImage *input,
                     void *const *outputs,
                     const size_t *bytes);

#ifdef __cplusplus
}
#endif

#endif
