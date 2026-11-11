/**
 * @file    plt_npu.cpp
 * @brief   RKNN 推理。输入用图像的 fd，输出拷进调用方的缓冲。
 */
#include "plt/plt_npu.h"

#include "plt_internal.h"

#include "rknn_api.h"

#include <stdio.h>
#include <string.h>

#include <new>
#include <vector>

struct PltNpu
{
    rknn_context ctx;
    rknn_tensor_attr input;
    std::vector<rknn_tensor_attr> outputs;
    int width;
    int height;

    PltNpu()
        : ctx(0)
        , width(0)
        , height(0)
    {
        memset(&input, 0, sizeof(input));
    }
};

#if !defined(RKNN_NPU_CORE_0)
#define PLT_RKNN_CORE_DECLARED 1
enum
{
    RKNN_NPU_CORE_AUTO = 0,
    RKNN_NPU_CORE_0 = 1,
    RKNN_NPU_CORE_1 = 2,
    RKNN_NPU_CORE_2 = 4,
    RKNN_NPU_CORE_0_1 = 3,
    RKNN_NPU_CORE_0_1_2 = 7,
    RKNN_NPU_CORE_ALL = 0xffff
};

extern "C" int rknn_set_core_mask(rknn_context ctx, int core_mask);
#endif

namespace
{

void TensorHw(const rknn_tensor_attr *attr, int *width, int *height)
{
    *width = 0;
    *height = 0;
    if (attr->n_dims < 4)
    {
        return;
    }
    if (attr->fmt == RKNN_TENSOR_NCHW)
    {
        *height = (int)attr->dims[2];
        *width = (int)attr->dims[3];
        return;
    }
    *height = (int)attr->dims[1];
    *width = (int)attr->dims[2];
}

int TensorType(int type)
{
    switch (type)
    {
    case RKNN_TENSOR_FLOAT32:
        return PLT_NPU_FLOAT32;
    case RKNN_TENSOR_FLOAT16:
        return PLT_NPU_FLOAT16;
    case RKNN_TENSOR_INT8:
        return PLT_NPU_INT8;
    case RKNN_TENSOR_UINT8:
        return PLT_NPU_UINT8;
    case RKNN_TENSOR_INT16:
        return PLT_NPU_INT16;
    case RKNN_TENSOR_UINT16:
        return PLT_NPU_UINT16;
    case RKNN_TENSOR_INT32:
        return PLT_NPU_INT32;
    case RKNN_TENSOR_UINT32:
        return PLT_NPU_UINT32;
    case RKNN_TENSOR_INT64:
        return PLT_NPU_INT64;
    default:
        return PLT_NPU_TYPE_UNKNOWN;
    }
}

int QuantType(int type)
{
    switch (type)
    {
    case RKNN_TENSOR_QNT_NONE:
        return PLT_NPU_QNT_NONE;
    case RKNN_TENSOR_QNT_DFP:
        return PLT_NPU_QNT_DFP;
    case RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC:
        return PLT_NPU_QNT_AFFINE;
    default:
        return PLT_NPU_QNT_UNKNOWN;
    }
}

void FillTensor(const rknn_tensor_attr *attr, PltNpuTensor *info)
{
    memset(info, 0, sizeof(*info));
    info->index = (int)attr->index;
    info->n_dims = (int)attr->n_dims;
    if (info->n_dims > 16)
    {
        info->n_dims = 16;
    }
    for (int i = 0; i < info->n_dims; ++i)
    {
        info->dims[i] = (int)attr->dims[i];
    }
    info->type = TensorType((int)attr->type);
    info->qnt_type = QuantType((int)attr->qnt_type);
    info->zp = (int)attr->zp;
    info->scale = attr->scale;
    info->n_elems = attr->n_elems;
    info->bytes = attr->size;
}

int BindCore(rknn_context ctx, int mask)
{
#if defined(PLT_RKNN_CORE_DECLARED)
    return rknn_set_core_mask(ctx, mask);
#else
    return rknn_set_core_mask(ctx, (rknn_core_mask)mask);
#endif
}

}

extern "C" BaseStatus plt_npu_open(const char *model_path, PltNpu **out)
{
    if (out != nullptr)
    {
        *out = nullptr;
    }
    if (model_path == nullptr || model_path[0] == '\0' || out == nullptr || !plt_rk_ready())
    {
        return BASE_ERR;
    }

    PltNpu *npu = new (std::nothrow) PltNpu();
    if (npu == nullptr)
    {
        return BASE_ERR;
    }
    if (rknn_init(&npu->ctx, (void *)model_path, 0, 0, nullptr) != RKNN_SUCC || npu->ctx == 0)
    {
        delete npu;
        return BASE_ERR;
    }

    rknn_input_output_num num;
    memset(&num, 0, sizeof(num));
    if (rknn_query(npu->ctx, RKNN_QUERY_IN_OUT_NUM, &num, sizeof(num)) != RKNN_SUCC || num.n_input != 1)
    {
        fprintf(stderr, "[plt.npu] model must have one input\n");
        rknn_destroy(npu->ctx);
        delete npu;
        return BASE_ERR;
    }

    npu->input.index = 0;
    if (rknn_query(npu->ctx, RKNN_QUERY_INPUT_ATTR, &npu->input, sizeof(npu->input)) != RKNN_SUCC)
    {
        rknn_destroy(npu->ctx);
        delete npu;
        return BASE_ERR;
    }
    TensorHw(&npu->input, &npu->width, &npu->height);

    try
    {
        npu->outputs.resize(num.n_output);
    }
    catch (const std::bad_alloc &)
    {
        rknn_destroy(npu->ctx);
        delete npu;
        return BASE_ERR;
    }
    for (uint32_t i = 0; i < num.n_output; ++i)
    {
        npu->outputs[i].index = i;
        if (rknn_query(npu->ctx, RKNN_QUERY_OUTPUT_ATTR, &npu->outputs[i], sizeof(npu->outputs[i])) != RKNN_SUCC)
        {
            rknn_destroy(npu->ctx);
            delete npu;
            return BASE_ERR;
        }
    }

    *out = npu;
    return BASE_OK;
}

extern "C" void plt_npu_close(PltNpu *npu)
{
    if (npu == nullptr)
    {
        return;
    }
    if (npu->ctx != 0)
    {
        rknn_destroy(npu->ctx);
    }
    delete npu;
}

extern "C" BaseStatus plt_npu_set_core(PltNpu *npu, int core_mask)
{
    if (npu == nullptr || npu->ctx == 0 || core_mask < 0)
    {
        return BASE_ERR;
    }
    if (BindCore(npu->ctx, core_mask) != RKNN_SUCC)
    {
        fprintf(stderr, "[plt.npu] set core failed\n");
        return BASE_ERR;
    }
    return BASE_OK;
}

extern "C" BaseStatus plt_npu_input_info(PltNpu *npu, PltNpuTensor *info)
{
    if (npu == nullptr || info == nullptr)
    {
        return BASE_ERR;
    }
    FillTensor(&npu->input, info);
    return BASE_OK;
}

extern "C" BaseStatus plt_npu_output_count(PltNpu *npu, int *count)
{
    if (npu == nullptr || count == nullptr)
    {
        return BASE_ERR;
    }
    *count = (int)npu->outputs.size();
    return BASE_OK;
}

extern "C" BaseStatus plt_npu_output_info(PltNpu *npu, int index, PltNpuTensor *info)
{
    if (npu == nullptr || info == nullptr || index < 0 || index >= (int)npu->outputs.size())
    {
        return BASE_ERR;
    }
    FillTensor(&npu->outputs[(size_t)index], info);
    return BASE_OK;
}

extern "C" BaseStatus plt_npu_run(PltNpu *npu, const BaseImage *input, void *const *outputs, const size_t *bytes)
{
    if (npu == nullptr || npu->ctx == 0 || !base_image_valid(input))
    {
        return BASE_ERR;
    }
    if (input->format != BASE_PIX_RGB888 || input->fd < 0 || input->size <= 0)
    {
        return BASE_ERR;
    }
    if (npu->width > 0 && (input->width != npu->width || input->height != npu->height))
    {
        return BASE_ERR;
    }

    const int count = (int)npu->outputs.size();
    if (count > 0 && (outputs == nullptr || bytes == nullptr))
    {
        return BASE_ERR;
    }
    for (int i = 0; i < count; ++i)
    {
        if (outputs[i] == nullptr || bytes[i] < npu->outputs[(size_t)i].size)
        {
            return BASE_ERR;
        }
    }

    rknn_tensor_attr attr = npu->input;
    attr.pass_through = 0;
    attr.type = RKNN_TENSOR_UINT8;
    attr.fmt = RKNN_TENSOR_NHWC;
    rknn_tensor_mem *mem = rknn_create_mem_from_fd(npu->ctx, input->fd, input->ptr, (uint32_t)input->size, 0);
    if (mem == nullptr)
    {
        fprintf(stderr, "[plt.npu] import fd failed\n");
        return BASE_ERR;
    }
    if (rknn_set_io_mem(npu->ctx, mem, &attr) != RKNN_SUCC)
    {
        rknn_destory_mem(npu->ctx, mem);
        fprintf(stderr, "[plt.npu] set input failed\n");
        return BASE_ERR;
    }
    if (rknn_run(npu->ctx, nullptr) != RKNN_SUCC)
    {
        rknn_destory_mem(npu->ctx, mem);
        fprintf(stderr, "[plt.npu] run failed\n");
        return BASE_ERR;
    }

    BaseStatus status = BASE_OK;
    if (count > 0)
    {
        try
        {
            std::vector<rknn_output> out((size_t)count);
            for (int i = 0; i < count; ++i)
            {
                memset(&out[(size_t)i], 0, sizeof(out[(size_t)i]));
                out[(size_t)i].want_float = 0;
                out[(size_t)i].is_prealloc = 1;
                out[(size_t)i].index = (uint32_t)i;
                out[(size_t)i].buf = outputs[i];
                out[(size_t)i].size = (uint32_t)bytes[i];
            }
            if (rknn_outputs_get(npu->ctx, (uint32_t)count, out.data(), nullptr) != RKNN_SUCC)
            {
                fprintf(stderr, "[plt.npu] get output failed\n");
                status = BASE_ERR;
            }
            rknn_outputs_release(npu->ctx, (uint32_t)count, out.data());
        }
        catch (const std::bad_alloc &)
        {
            status = BASE_ERR;
        }
    }
    rknn_destory_mem(npu->ctx, mem);
    return status;
}
