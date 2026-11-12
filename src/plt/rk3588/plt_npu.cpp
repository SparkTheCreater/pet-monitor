/**
 * @file    plt_npu.cpp
 * @brief   RKNN 推理。紧密 RGB 用 fd 送入，行宽更大时先收紧。输出拷进调用方的缓冲。
 */
#include "plt/plt_npu.h"

#include "plt/plt_mem.h"
#include "plt_internal.h"

#include "rknn_api.h"

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

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

namespace
{

void TensorHw(const rknn_tensor_attr* attr, int* width, int* height)
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

int TensorFmt(int fmt)
{
    switch (fmt)
    {
    case RKNN_TENSOR_NCHW:
        return PLT_NPU_FMT_NCHW;
    case RKNN_TENSOR_NHWC:
        return PLT_NPU_FMT_NHWC;
    case RKNN_TENSOR_NC1HWC2:
        return PLT_NPU_FMT_NC1HWC2;
    default:
        return PLT_NPU_FMT_UNKNOWN;
    }
}

void FillTensor(const rknn_tensor_attr* attr, PltNpuTensor* info)
{
    memset(info, 0, sizeof(*info));
    info->index = (int)attr->index;
    info->nDims = (int)attr->n_dims;
    if (info->nDims > 16)
    {
        info->nDims = 16;
    }
    for (int i = 0; i < info->nDims; ++i)
    {
        info->dims[i] = (int)attr->dims[i];
    }
    info->type = TensorType((int)attr->type);
    info->fmt = TensorFmt((int)attr->fmt);
    info->qntType = QuantType((int)attr->qnt_type);
    info->fl = (int)attr->fl;
    info->zp = (int)attr->zp;
    info->scale = attr->scale;
    info->nElems = attr->n_elems;
    info->bytes = attr->size;
}

int BindCore(rknn_context ctx, int mask)
{
    return rknn_set_core_mask(ctx, (rknn_core_mask)mask);
}

struct DmaSync
{
    uint64_t flags;
};

#ifndef PLT_DMA_BUF_IOCTL_SYNC
#define PLT_DMA_BUF_BASE 'b'
#define PLT_DMA_BUF_IOCTL_SYNC _IOW(PLT_DMA_BUF_BASE, 0, DmaSync)
#endif

/* READ|WRITE，再加上 START 或 END。CPU 读之前先写回并作废 cache，读完再结束。 */
const uint64_t kCpuBegin = 1u | 2u;
const uint64_t kCpuEnd = 1u | 2u | 4u;

int RowStride(const BaseImage* image)
{
    if (image->hor_stride > 0)
    {
        return image->hor_stride;
    }
    return image->width;
}

int RgbCovers(const BaseImage* image, int hor)
{
    const int ver = image->ver_stride > 0 ? image->ver_stride : image->height;
    if (image->width <= 0 || image->height <= 0 || hor < image->width || ver < image->height)
    {
        return 0;
    }
    const uint64_t bytes = (uint64_t)hor * (uint64_t)image->height * 3u;
    return (uint64_t)image->size >= bytes;
}

int SyncCpu(const BaseImage* image, uint64_t flags)
{
    DmaSync sync;
    sync.flags = flags;
    return ioctl(image->fd, PLT_DMA_BUF_IOCTL_SYNC, &sync) == 0;
}

void UserInput(rknn_tensor_attr* attr, int width, int height)
{
    attr->pass_through = 0;
    attr->type = RKNN_TENSOR_UINT8;
    attr->fmt = RKNN_TENSOR_NHWC;
    attr->w_stride = 0;
    attr->h_stride = 0;
    if (width <= 0 || height <= 0)
    {
        return;
    }
    attr->size = (uint32_t)width * (uint32_t)height * 3u;
    attr->n_elems = attr->size;
}

BaseStatus TakeOutputs(PltNpu* npu, void* const* outputs, const size_t* bytes)
{
    const int count = (int)npu->outputs.size();
    if (count == 0)
    {
        return BASE_OK;
    }
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
            rknn_outputs_release(npu->ctx, (uint32_t)count, out.data());
            return BASE_ERR;
        }
        rknn_outputs_release(npu->ctx, (uint32_t)count, out.data());
        return BASE_OK;
    }
    catch (const std::bad_alloc &)
    {
        return BASE_ERR;
    }
}

BaseStatus RunPacked(PltNpu* npu, const BaseImage* input, int hor, void* const* outputs, const size_t* bytes)
{
    if (input->ptr == nullptr || !SyncCpu(input, kCpuBegin))
    {
        fprintf(stderr, "[plt.npu] packed input unavailable\n");
        return BASE_ERR;
    }
    const size_t dst_stride = (size_t)input->width * 3;
    const size_t src_stride = (size_t)hor * 3;
    const size_t tight = dst_stride * (size_t)input->height;
    std::vector<uint8_t> packed;
    try
    {
        packed.resize(tight);
    }
    catch (const std::bad_alloc &)
    {
        return BASE_ERR;
    }
    const uint8_t* src = (const uint8_t*)input->ptr;
    for (int y = 0; y < input->height; ++y)
    {
        memcpy(packed.data() + (size_t)y * dst_stride, src + (size_t)y * src_stride, dst_stride);
    }
    if (!SyncCpu(input, kCpuEnd))
    {
        fprintf(stderr, "[plt.npu] packed input unavailable\n");
        return BASE_ERR;
    }
    rknn_input in;
    memset(&in, 0, sizeof(in));
    in.index = 0;
    in.buf = packed.data();
    in.size = (uint32_t)tight;
    in.pass_through = 0;
    in.type = RKNN_TENSOR_UINT8;
    in.fmt = RKNN_TENSOR_NHWC;
    if (rknn_inputs_set(npu->ctx, 1, &in) != RKNN_SUCC)
    {
        fprintf(stderr, "[plt.npu] set input failed\n");
        return BASE_ERR;
    }
    if (rknn_run(npu->ctx, nullptr) != RKNN_SUCC)
    {
        fprintf(stderr, "[plt.npu] run failed\n");
        return BASE_ERR;
    }
    return TakeOutputs(npu, outputs, bytes);
}

}

extern "C" BaseStatus plt_npu_open(const char* modelPath, PltNpu** out)
{
    if (out != nullptr)
    {
        *out = nullptr;
    }
    if (modelPath == nullptr || modelPath[0] == '\0' || out == nullptr || !plt_rk_ready())
    {
        return BASE_ERR;
    }

    PltNpu* npu = new (std::nothrow) PltNpu();
    if (npu == nullptr)
    {
        return BASE_ERR;
    }
    if (rknn_init(&npu->ctx, (void*)modelPath, 0, 0, nullptr) != RKNN_SUCC || npu->ctx == 0)
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

extern "C" void plt_npu_close(PltNpu* npu)
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

extern "C" BaseStatus plt_npu_set_core(PltNpu* npu, int coreMask)
{
    if (npu == nullptr || npu->ctx == 0 || coreMask < 0)
    {
        return BASE_ERR;
    }
    if (BindCore(npu->ctx, coreMask) != RKNN_SUCC)
    {
        fprintf(stderr, "[plt.npu] set core failed\n");
        return BASE_ERR;
    }
    return BASE_OK;
}

extern "C" BaseStatus plt_npu_input_info(PltNpu* npu, PltNpuTensor* info)
{
    if (npu == nullptr || info == nullptr)
    {
        return BASE_ERR;
    }
    FillTensor(&npu->input, info);
    return BASE_OK;
}

extern "C" BaseStatus plt_npu_output_count(PltNpu* npu, int* count)
{
    if (npu == nullptr || count == nullptr)
    {
        return BASE_ERR;
    }
    *count = (int)npu->outputs.size();
    return BASE_OK;
}

extern "C" BaseStatus plt_npu_output_info(PltNpu* npu, int index, PltNpuTensor* info)
{
    if (npu == nullptr || info == nullptr || index < 0 || index >= (int)npu->outputs.size())
    {
        return BASE_ERR;
    }
    FillTensor(&npu->outputs[(size_t)index], info);
    return BASE_OK;
}

extern "C" BaseStatus plt_npu_run(PltNpu* npu, const BaseImage* input, void* const* outputs, const size_t* bytes)
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
    const int hor = RowStride(input);
    if (!RgbCovers(input, hor))
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

    if (hor != input->width)
    {
        return RunPacked(npu, input, hor, outputs, bytes);
    }
    if (plt_mem_flush(const_cast<BaseImage*>(input)) != BASE_OK)
    {
        fprintf(stderr, "[plt.npu] flush input failed\n");
        return BASE_ERR;
    }

    rknn_tensor_attr attr = npu->input;
    UserInput(&attr, npu->width > 0 ? npu->width : input->width, npu->height > 0 ? npu->height : input->height);
    rknn_tensor_mem *mem = rknn_create_mem_from_fd(npu->ctx, input->fd, input->ptr, (uint32_t)input->size, 0);
    if (mem == nullptr)
    {
        fprintf(stderr, "[plt.npu] import fd failed\n");
        return BASE_ERR;
    }
    if (rknn_set_io_mem(npu->ctx, mem, &attr) != RKNN_SUCC)
    {
        rknn_destroy_mem(npu->ctx, mem);
        fprintf(stderr, "[plt.npu] set input failed\n");
        return BASE_ERR;
    }
    BaseStatus status = BASE_OK;
    if (rknn_run(npu->ctx, nullptr) != RKNN_SUCC)
    {
        fprintf(stderr, "[plt.npu] run failed\n");
        status = BASE_ERR;
    }
    else
    {
        status = TakeOutputs(npu, outputs, bytes);
    }
    rknn_destroy_mem(npu->ctx, mem);
    return status;
}
