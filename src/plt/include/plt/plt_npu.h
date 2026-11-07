/**
 * @file    plt_npu.h
 * @brief   加载模型并跑一次推理。不做检测后处理。
 */
#ifndef PM_PLT_NPU_H
#define PM_PLT_NPU_H

#include "pm_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct PltNpu PltNpu;

/*
 * 一个输入或输出张量。type 的数值与 RKNN 的 rknn_tensor_type 相同。
 * 调用方按 bytes 准备缓冲，不要从 type 自行推断字节布局。
 */
typedef struct PltNpuTensor
{
    int index;
    int n_dims;
    int dims[16];
    int type;
    uint32_t n_elems;
    uint32_t bytes;
}
PltNpuTensor;

/* 同步。从文件加载模型。只接受单输入模型。失败时 *out 为空。 */
PmStatus plt_npu_open(const char *model_path, PltNpu **out);

/* 同步。卸载模型。空指针直接返回。 */
void plt_npu_close(PltNpu *npu);

/*
 * 同步。把以后的推理绑到指定核。mask 的数值与 RKNN 的 rknn_core_mask 相同，0 表示自动。
 * 没有这个能力的运行时返回 PM_ERR。
 */
PmStatus plt_npu_set_core(PltNpu *npu, int core_mask);

/* 同步。读模型输入的形状。 */
PmStatus plt_npu_input_info(PltNpu *npu, PltNpuTensor *info);

/* 同步。写出输出个数。 */
PmStatus plt_npu_output_count(PltNpu *npu, int *count);

/* 同步。读第 index 个输出的形状。 */
PmStatus plt_npu_output_info(PltNpu *npu, int index, PltNpuTensor *info);

/*
 * 同步。input 必须是 RGB888，宽高与模型输入一致，用 fd 直接交给 NPU，中间不经 CPU 拷贝。
 * 返回之前不得改写 input。outputs[i] 由调用方提供，bytes[i] 不得小于该输出的 bytes。
 * 原始输出拷进调用方的缓冲，不在这里做阈值和画框。
 */
PmStatus plt_npu_run(PltNpu *npu,
                     const PmImage *input,
                     void *const *outputs,
                     const size_t *bytes);

#ifdef __cplusplus
}
#endif

#endif
