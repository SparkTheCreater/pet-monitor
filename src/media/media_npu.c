/**
 * @file    media_npu.c
 * @brief   推理交给平台层。这里只转一层句柄和张量。
 */
#include "media_npu.h"

#include "plt/plt_npu.h"

#include <stdlib.h>
#include <string.h>

_Static_assert((int)MEDIA_NPU_FLOAT32 == (int)PLT_NPU_FLOAT32, "float32");
_Static_assert((int)MEDIA_NPU_FLOAT16 == (int)PLT_NPU_FLOAT16, "float16");
_Static_assert((int)MEDIA_NPU_INT8 == (int)PLT_NPU_INT8, "int8");
_Static_assert((int)MEDIA_NPU_UINT8 == (int)PLT_NPU_UINT8, "uint8");
_Static_assert((int)MEDIA_NPU_INT16 == (int)PLT_NPU_INT16, "int16");
_Static_assert((int)MEDIA_NPU_UINT16 == (int)PLT_NPU_UINT16, "uint16");
_Static_assert((int)MEDIA_NPU_INT32 == (int)PLT_NPU_INT32, "int32");
_Static_assert((int)MEDIA_NPU_UINT32 == (int)PLT_NPU_UINT32, "uint32");
_Static_assert((int)MEDIA_NPU_INT64 == (int)PLT_NPU_INT64, "int64");
_Static_assert((int)MEDIA_NPU_TYPE_UNKNOWN == (int)PLT_NPU_TYPE_UNKNOWN, "type");
_Static_assert((int)MEDIA_NPU_QNT_NONE == (int)PLT_NPU_QNT_NONE, "qnt none");
_Static_assert((int)MEDIA_NPU_QNT_DFP == (int)PLT_NPU_QNT_DFP, "qnt dfp");
_Static_assert((int)MEDIA_NPU_QNT_AFFINE == (int)PLT_NPU_QNT_AFFINE, "qnt affine");
_Static_assert((int)MEDIA_NPU_QNT_UNKNOWN == (int)PLT_NPU_QNT_UNKNOWN, "qnt");
_Static_assert((int)MEDIA_NPU_FMT_NCHW == (int)PLT_NPU_FMT_NCHW, "fmt nchw");
_Static_assert((int)MEDIA_NPU_FMT_NHWC == (int)PLT_NPU_FMT_NHWC, "fmt nhwc");
_Static_assert((int)MEDIA_NPU_FMT_NC1HWC2 == (int)PLT_NPU_FMT_NC1HWC2, "fmt nc1hwc2");
_Static_assert((int)MEDIA_NPU_FMT_UNKNOWN == (int)PLT_NPU_FMT_UNKNOWN, "fmt");

struct MediaNpu
{
    PltNpu *npu;
};

static PltNpu *Inner(MediaNpu *npu)
{
    if (npu == NULL)
    {
        return NULL;
    }
    return npu->npu;
}

static void CopyTensor(const PltNpuTensor *src, MediaNpuTensor *dst)
{
    int i;
    int n;

    memset(dst, 0, sizeof(*dst));
    dst->index = src->index;
    dst->n_dims = src->nDims;
    n = src->nDims;
    if (n > 16)
    {
        n = 16;
    }
    if (n < 0)
    {
        n = 0;
    }
    for (i = 0; i < n; ++i)
    {
        dst->dims[i] = src->dims[i];
    }
    dst->type = src->type;
    dst->fmt = src->fmt;
    dst->qnt_type = src->qntType;
    dst->fl = src->fl;
    dst->zp = src->zp;
    dst->scale = src->scale;
    dst->n_elems = src->nElems;
    dst->bytes = src->bytes;
}

BaseStatus media_npu_open(const char *model_path, MediaNpu **out)
{
    PltNpu *npu;
    MediaNpu *media;
    BaseStatus status;

    if (out != NULL)
    {
        *out = NULL;
    }
    if (out == NULL)
    {
        return BASE_ERR;
    }
    npu = NULL;
    status = plt_npu_open(model_path, &npu);
    if (status != BASE_OK)
    {
        return status;
    }
    media = (MediaNpu *)calloc(1, sizeof(*media));
    if (media == NULL)
    {
        plt_npu_close(npu);
        return BASE_ERR;
    }
    media->npu = npu;
    *out = media;
    return BASE_OK;
}

void media_npu_close(MediaNpu *npu)
{
    if (npu == NULL)
    {
        return;
    }
    plt_npu_close(npu->npu);
    free(npu);
}

BaseStatus media_npu_set_core(MediaNpu *npu, int core_mask)
{
    return plt_npu_set_core(Inner(npu), core_mask);
}

BaseStatus media_npu_input_info(MediaNpu *npu, MediaNpuTensor *info)
{
    PltNpuTensor tensor;
    BaseStatus status;

    if (info == NULL)
    {
        return plt_npu_input_info(Inner(npu), NULL);
    }
    memset(&tensor, 0, sizeof(tensor));
    status = plt_npu_input_info(Inner(npu), &tensor);
    CopyTensor(&tensor, info);
    return status;
}

BaseStatus media_npu_output_count(MediaNpu *npu, int *count)
{
    return plt_npu_output_count(Inner(npu), count);
}

BaseStatus media_npu_output_info(MediaNpu *npu, int index, MediaNpuTensor *info)
{
    PltNpuTensor tensor;
    BaseStatus status;

    if (info == NULL)
    {
        return plt_npu_output_info(Inner(npu), index, NULL);
    }
    memset(&tensor, 0, sizeof(tensor));
    status = plt_npu_output_info(Inner(npu), index, &tensor);
    CopyTensor(&tensor, info);
    return status;
}

BaseStatus media_npu_run(MediaNpu *npu, const BaseImage *input, void *const *outputs, const size_t *bytes)
{
    return plt_npu_run(Inner(npu), input, outputs, bytes);
}
