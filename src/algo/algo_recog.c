/**
 * @file    algo_recog.c
 * @brief   识别算法。整图缩放到模型输入，取得分最高的一类。
 */
#include "algo_internal.h"

#include "media_mem.h"
#include "media_npu.h"
#include "media_pic.h"

#include <stdlib.h>
#include <string.h>

static const int kMaxClass = 1024;

struct AiRecog
{
    MediaNpu *npu;
    int model_w;
    int model_h;
    int n_cls;
    BaseImage canvas;
    void *buf;
    size_t bytes;
    MediaNpuTensor out;
};

static int InputSize(const MediaNpuTensor *in, int *width, int *height)
{
    if (in->n_dims < 4)
    {
        return 0;
    }
    if (in->fmt == MEDIA_NPU_FMT_NCHW)
    {
        *height = in->dims[2];
        *width = in->dims[3];
        return *width > 0 && *height > 0;
    }
    if (in->fmt == MEDIA_NPU_FMT_NHWC)
    {
        *height = in->dims[1];
        *width = in->dims[2];
        return *width > 0 && *height > 0;
    }
    return 0;
}

static float Dequant(const void *base, int index, const MediaNpuTensor *t)
{
    if (t->type == MEDIA_NPU_FLOAT32)
    {
        return ((const float *)base)[index];
    }
    if (t->qnt_type == MEDIA_NPU_QNT_AFFINE && t->type == MEDIA_NPU_INT8)
    {
        return ((float)((const int8_t *)base)[index] - (float)t->zp) * t->scale;
    }
    if (t->qnt_type == MEDIA_NPU_QNT_AFFINE && t->type == MEDIA_NPU_UINT8)
    {
        return ((float)((const uint8_t *)base)[index] - (float)t->zp) * t->scale;
    }
    if (t->qnt_type == MEDIA_NPU_QNT_DFP && t->type == MEDIA_NPU_INT8)
    {
        int shift = t->fl;
        float q = (float)((const int8_t *)base)[index];
        if (shift >= 0 && shift < 31)
        {
            return q / (float)(1u << shift);
        }
        if (shift < 0 && shift > -31)
        {
            return q * (float)(1u << (unsigned)(-shift));
        }
    }
    return 0.0f;
}

static void Release(AiRecog *recog)
{
    if (recog == NULL)
    {
        return;
    }
    free(recog->buf);
    media_mem_free(&recog->canvas);
    media_npu_close(recog->npu);
    free(recog);
}

BaseStatus ai_recog_open(const char *model_path, const AlgoOpenParam *param, AiRecog **out)
{
    AiRecog *recog;
    MediaNpuTensor input;
    int count;
    if (out != NULL)
    {
        *out = NULL;
    }
    if (model_path == NULL || out == NULL)
    {
        return BASE_ERR;
    }
    recog = (AiRecog *)calloc(1, sizeof(*recog));
    if (recog == NULL)
    {
        return BASE_ERR;
    }
    if (media_npu_open(model_path, &recog->npu) != BASE_OK)
    {
        Release(recog);
        return BASE_ERR;
    }
    if (param != NULL && param->dev_id > 0 && media_npu_set_core(recog->npu, param->dev_id) != BASE_OK)
    {
        Release(recog);
        return BASE_ERR;
    }
    if (media_npu_input_info(recog->npu, &input) != BASE_OK || !InputSize(&input, &recog->model_w, &recog->model_h))
    {
        Release(recog);
        return BASE_ERR;
    }
    if (media_npu_output_count(recog->npu, &count) != BASE_OK || count != 1 ||
        media_npu_output_info(recog->npu, 0, &recog->out) != BASE_OK)
    {
        Release(recog);
        return BASE_ERR;
    }
    if (recog->out.n_elems == 0 || recog->out.n_elems > (uint32_t)kMaxClass || recog->out.bytes == 0)
    {
        Release(recog);
        return BASE_ERR;
    }
    recog->n_cls = (int)recog->out.n_elems;
    recog->bytes = recog->out.bytes;
    recog->buf = malloc(recog->bytes);
    if (recog->buf == NULL || media_mem_alloc(recog->model_w, recog->model_h, BASE_PIX_RGB888, &recog->canvas) != BASE_OK)
    {
        Release(recog);
        return BASE_ERR;
    }
    *out = recog;
    return BASE_OK;
}

void ai_recog_close(AiRecog *recog)
{
    Release(recog);
}

BaseStatus ai_recog_preprocess(AiRecog *recog, const BaseImage *frame)
{
    if (recog == NULL || !base_image_valid(frame))
    {
        return BASE_ERR;
    }
    return media_pic_scale(frame, NULL, &recog->canvas, NULL);
}

BaseStatus ai_recog_infer(AiRecog *recog)
{
    void *output;
    if (recog == NULL)
    {
        return BASE_ERR;
    }
    output = recog->buf;
    return media_npu_run(recog->npu, &recog->canvas, &output, &recog->bytes);
}

BaseStatus ai_recog_postprocess(AiRecog *recog, AlgoRecog *out)
{
    int i;
    int best;
    float best_score;
    float sum;
    if (recog == NULL || out == NULL)
    {
        return BASE_ERR;
    }
    best = 0;
    best_score = Dequant(recog->buf, 0, &recog->out);
    sum = 0.0f;
    for (i = 0; i < recog->n_cls; ++i)
    {
        float score = Dequant(recog->buf, i, &recog->out);
        if (score > best_score)
        {
            best_score = score;
            best = i;
        }
        if (score > 0.0f)
        {
            sum += score;
        }
    }
    out->cls = best;
    if (sum > 0.0f && best_score > 0.0f)
    {
        out->score = best_score / sum;
    }
    else
    {
        out->score = best_score;
    }
    return BASE_OK;
}

static BaseStatus RecogOpen(const char *model_path, const AlgoOpenParam *param, void **impl)
{
    return ai_recog_open(model_path, param, (AiRecog **)impl);
}

static void RecogClose(void *impl)
{
    ai_recog_close((AiRecog *)impl);
}

static BaseStatus RecogPre(void *impl, const BaseImage *frame)
{
    return ai_recog_preprocess((AiRecog *)impl, frame);
}

static BaseStatus RecogInfer(void *impl)
{
    return ai_recog_infer((AiRecog *)impl);
}

static BaseStatus RecogPost(void *impl, void *buf, int buf_size)
{
    if (buf == NULL || buf_size < (int)sizeof(AlgoRecog))
    {
        return BASE_ERR;
    }
    return ai_recog_postprocess((AiRecog *)impl, (AlgoRecog *)buf);
}

void ai_recog_register(void)
{
    static const AlgoModule mod =
    {
        ALGO_RECOGNIZE,
        "recognize",
        (int)sizeof(AlgoRecog),
        RecogOpen,
        RecogClose,
        RecogPre,
        RecogInfer,
        RecogPost,
        NULL
    };
    algo_register_mod(&mod);
}
