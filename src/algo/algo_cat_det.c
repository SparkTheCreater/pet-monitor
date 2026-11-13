/**
 * @file    algo_cat_det.c
 * @brief   猫检测。和检测、追踪、识别一起登记，种类号 ALGO_CAT_DET。
 *
 * 模型由 algo_add 传入。配套的是 models/Catto.rknn：
 * 一个输出，每行 xywh、obj、cls，xywh 在补灰后的模型图上。
 */
#include "algo_internal.h"

#include "media_mem.h"
#include "media_npu.h"
#include "media_pic.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const float kScoreMin = 0.25f;
static const float kNmsIou = 0.45f;

typedef struct
{
    float x;
    float y;
    float w;
    float h;
    float score;
}
Cand;

typedef struct
{
    MediaNpu *npu;
    MediaNpuTensor info;
    int model_w;
    int model_h;
    int rows;
    int row_major;
    void *buf;
    size_t bytes;
    Cand *cand;
    int cand_cap;
    BaseImage canvas;
    const BaseImage *frame;
    MediaPicPreprocess prep;
    float score_min;
}
CatDet;

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

static int OutputRows(const MediaNpuTensor *t, int *rows, int *row_major)
{
    int i;
    int six;
    int n;
    six = -1;
    n = 1;
    if (t->n_dims < 2)
    {
        return 0;
    }
    for (i = 0; i < t->n_dims; ++i)
    {
        if (t->dims[i] == 6 && six < 0)
        {
            six = i;
            continue;
        }
        if (t->dims[i] > 1)
        {
            n *= t->dims[i];
        }
    }
    if (six < 0 || n < 1)
    {
        return 0;
    }
    *rows = n;
    *row_major = six == t->n_dims - 1;
    return 1;
}

static float HalfToFloat(uint16_t bits)
{
    uint32_t sign;
    uint32_t exp;
    uint32_t mant;
    uint32_t out;
    float value;
    sign = ((uint32_t)bits & 0x8000u) << 16;
    exp = (bits >> 10) & 0x1fu;
    mant = bits & 0x3ffu;
    if (exp == 0)
    {
        if (mant == 0)
        {
            out = sign;
        }
        else
        {
            exp = 1;
            while ((mant & 0x400u) == 0)
            {
                mant <<= 1;
                exp -= 1;
            }
            mant &= 0x3ffu;
            out = sign | ((exp + 112) << 23) | (mant << 13);
        }
    }
    else if (exp == 31)
    {
        out = sign | 0x7f800000u | (mant << 13);
    }
    else
    {
        out = sign | ((exp + 112) << 23) | (mant << 13);
    }
    memcpy(&value, &out, sizeof(value));
    return value;
}

static float Dequant(const void *base, int index, const MediaNpuTensor *t)
{
    if (t->type == MEDIA_NPU_FLOAT32)
    {
        return ((const float *)base)[index];
    }
    if (t->type == MEDIA_NPU_FLOAT16)
    {
        return HalfToFloat(((const uint16_t *)base)[index]);
    }
    if (t->qnt_type == MEDIA_NPU_QNT_AFFINE && t->type == MEDIA_NPU_INT8)
    {
        return ((float)((const int8_t *)base)[index] - (float)t->zp) * t->scale;
    }
    if (t->qnt_type == MEDIA_NPU_QNT_AFFINE && t->type == MEDIA_NPU_UINT8)
    {
        return ((float)((const uint8_t *)base)[index] - (float)t->zp) * t->scale;
    }
    return 0.0f;
}

static float At(const CatDet *det, int row, int item)
{
    int index;
    if (det->row_major)
    {
        index = row * 6 + item;
    }
    else
    {
        index = item * det->rows + row;
    }
    return Dequant(det->buf, index, &det->info);
}

static int ClampInt(int value, int lo, int hi)
{
    if (value < lo)
    {
        return lo;
    }
    if (value > hi)
    {
        return hi;
    }
    return value;
}

static int MapBox(const Cand *cand, const BaseImage *src, const MediaPicPreprocess *prep, BaseRect *box)
{
    float x1 = (cand->x - (float)prep->x_pad) * (float)src->width / (float)prep->content_w;
    float y1 = (cand->y - (float)prep->y_pad) * (float)src->height / (float)prep->content_h;
    float x2 = (cand->x + cand->w - (float)prep->x_pad) * (float)src->width / (float)prep->content_w;
    float y2 = (cand->y + cand->h - (float)prep->y_pad) * (float)src->height / (float)prep->content_h;
    int ix1 = ClampInt((int)(x1 >= 0.0f ? x1 + 0.5f : x1), 0, src->width);
    int iy1 = ClampInt((int)(y1 >= 0.0f ? y1 + 0.5f : y1), 0, src->height);
    int ix2 = ClampInt((int)(x2 >= 0.0f ? x2 + 0.5f : x2), 0, src->width);
    int iy2 = ClampInt((int)(y2 >= 0.0f ? y2 + 0.5f : y2), 0, src->height);
    if (ix2 <= ix1 || iy2 <= iy1)
    {
        return 0;
    }
    box->x = ix1;
    box->y = iy1;
    box->w = ix2 - ix1;
    box->h = iy2 - iy1;
    return 1;
}

static float Iou(const Cand *a, const Cand *b)
{
    float ax2 = a->x + a->w;
    float ay2 = a->y + a->h;
    float bx2 = b->x + b->w;
    float by2 = b->y + b->h;
    float x1 = a->x > b->x ? a->x : b->x;
    float y1 = a->y > b->y ? a->y : b->y;
    float x2 = ax2 < bx2 ? ax2 : bx2;
    float y2 = ay2 < by2 ? ay2 : by2;
    float iw = x2 - x1;
    float ih = y2 - y1;
    float inter;
    float uni;
    if (iw <= 0.0f || ih <= 0.0f || a->w <= 0.0f || a->h <= 0.0f || b->w <= 0.0f || b->h <= 0.0f)
    {
        return 0.0f;
    }
    inter = iw * ih;
    uni = a->w * a->h + b->w * b->h - inter;
    if (uni <= 0.0f)
    {
        return 0.0f;
    }
    return inter / uni;
}

static int ScoreDesc(const void *left, const void *right)
{
    float a = ((const Cand *)left)->score;
    float b = ((const Cand *)right)->score;
    if (a < b)
    {
        return 1;
    }
    if (a > b)
    {
        return -1;
    }
    return 0;
}

static void Release(CatDet *det)
{
    if (det == NULL)
    {
        return;
    }
    free(det->buf);
    free(det->cand);
    media_mem_free(&det->canvas);
    media_npu_close(det->npu);
    free(det);
}

static BaseStatus Open(const char *model_path, const AlgoOpenParam *param, void **impl)
{
    CatDet *det;
    MediaNpuTensor input;
    int count;
    if (impl != NULL)
    {
        *impl = NULL;
    }
    if (model_path == NULL || impl == NULL)
    {
        return BASE_ERR;
    }
    det = (CatDet *)calloc(1, sizeof(*det));
    if (det == NULL)
    {
        return BASE_ERR;
    }
    if (media_npu_open(model_path, &det->npu) != BASE_OK)
    {
        Release(det);
        return BASE_ERR;
    }
    if (param != NULL && param->dev_id > 0 && media_npu_set_core(det->npu, param->dev_id) != BASE_OK)
    {
        Release(det);
        return BASE_ERR;
    }
    det->score_min = kScoreMin;
    if (param != NULL && param->score_min > 0.0f && param->score_min <= 1.0f)
    {
        det->score_min = param->score_min;
    }
    if (media_npu_input_info(det->npu, &input) != BASE_OK || !InputSize(&input, &det->model_w, &det->model_h))
    {
        Release(det);
        return BASE_ERR;
    }
    if (media_npu_output_count(det->npu, &count) != BASE_OK || count != 1)
    {
        Release(det);
        return BASE_ERR;
    }
    if (media_npu_output_info(det->npu, 0, &det->info) != BASE_OK ||
        !OutputRows(&det->info, &det->rows, &det->row_major) || det->info.bytes == 0)
    {
        Release(det);
        return BASE_ERR;
    }
    det->bytes = det->info.bytes;
    det->buf = malloc(det->bytes);
    det->cand = (Cand *)malloc((size_t)det->rows * sizeof(Cand));
    if (det->buf == NULL || det->cand == NULL)
    {
        Release(det);
        return BASE_ERR;
    }
    det->cand_cap = det->rows;
    if (media_mem_alloc(det->model_w, det->model_h, BASE_PIX_RGB888, &det->canvas) != BASE_OK)
    {
        Release(det);
        return BASE_ERR;
    }
    *impl = det;
    return BASE_OK;
}

static void Close(void *impl)
{
    Release((CatDet *)impl);
}

static BaseStatus Preprocess(void *impl, const BaseImage *frame)
{
    CatDet *det;
    det = (CatDet *)impl;
    if (det == NULL || !base_image_valid(frame) || frame->width <= 0 || frame->height <= 0)
    {
        return BASE_ERR;
    }
    if (media_pic_preprocess(frame, &det->canvas, &det->prep) != BASE_OK)
    {
        det->frame = NULL;
        return BASE_ERR;
    }
    det->frame = frame;
    return BASE_OK;
}

static BaseStatus Infer(void *impl)
{
    CatDet *det;
    void *out;
    det = (CatDet *)impl;
    if (det == NULL || det->frame == NULL)
    {
        return BASE_ERR;
    }
    out = det->buf;
    return media_npu_run(det->npu, &det->canvas, &out, &det->bytes);
}

static int Collect(const CatDet *det, Cand *cand, int cap)
{
    int row;
    int count;
    count = 0;
    for (row = 0; row < det->rows; ++row)
    {
        float cx;
        float cy;
        float bw;
        float bh;
        float score;
        if (count >= cap)
        {
            return count;
        }
        cx = At(det, row, 0);
        cy = At(det, row, 1);
        bw = At(det, row, 2);
        bh = At(det, row, 3);
        score = At(det, row, 4) * At(det, row, 5);
        if (score < det->score_min || bw <= 0.0f || bh <= 0.0f)
        {
            continue;
        }
        cand[count].x = cx - bw * 0.5f;
        cand[count].y = cy - bh * 0.5f;
        cand[count].w = bw;
        cand[count].h = bh;
        cand[count].score = score;
        count += 1;
    }
    return count;
}

static BaseStatus Postprocess(void *impl, void *buf, int buf_size)
{
    CatDet *det;
    AlgoDetectResult *result;
    int n;
    int i;
    int written;
    det = (CatDet *)impl;
    result = (AlgoDetectResult *)buf;
    if (result == NULL || buf_size < (int)sizeof(*result))
    {
        return BASE_ERR;
    }
    result->count = 0;
    if (det == NULL || det->frame == NULL || det->prep.content_w <= 0 || det->prep.content_h <= 0)
    {
        return BASE_ERR;
    }
    n = Collect(det, det->cand, det->cand_cap);
    if (n > 1)
    {
        qsort(det->cand, (size_t)n, sizeof(Cand), ScoreDesc);
    }
    for (i = 0; i < n; ++i)
    {
        int j;
        if (det->cand[i].score < 0.0f)
        {
            continue;
        }
        for (j = i + 1; j < n; ++j)
        {
            if (det->cand[j].score < 0.0f)
            {
                continue;
            }
            if (Iou(&det->cand[i], &det->cand[j]) > kNmsIou)
            {
                det->cand[j].score = -1.0f;
            }
        }
    }
    written = 0;
    for (i = 0; i < n && written < ALGO_MAX; ++i)
    {
        BaseRect box;
        if (det->cand[i].score < det->score_min)
        {
            continue;
        }
        if (!MapBox(&det->cand[i], det->frame, &det->prep, &box))
        {
            continue;
        }
        result->box[written].type = ALGO_CAT;
        result->box[written].score = det->cand[i].score;
        result->box[written].box = box;
        written += 1;
    }
    result->count = written;
    return BASE_OK;
}

static const AlgoModule g_mod =
{
    ALGO_CAT_DET,
    "cat_det",
    (int)sizeof(AlgoDetectResult),
    Open,
    Close,
    Preprocess,
    Infer,
    Postprocess,
    NULL
};

void algo_cat_det_register(void)
{
    algo_register_mod(&g_mod);
}
