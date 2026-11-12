/**
 * @file    algo_detect.c
 * @brief   检测算法。一帧原图在内部补灰缩放，再解 YOLOv5 的猫。
 */
#include "algo_internal.h"

#include "media_mem.h"
#include "media_npu.h"
#include "media_pic.h"

#include <stdlib.h>
#include <string.h>

/* COCO 80 类里，本模块对外只认猫。 */
static const int kCocoCat = 15;
static const int kClassCount = 80;
static const int kPerAnchor = 5 + 80;
static const int kAnchors = 3;
static const float kNmsIou = 0.45f;
static const float kScoreMin = 0.25f;

static const int kAnchor8[6] = {10, 13, 16, 30, 33, 23};
static const int kAnchor16[6] = {30, 61, 62, 45, 59, 119};
static const int kAnchor32[6] = {116, 90, 156, 198, 373, 326};

typedef struct
{
    float x;
    float y;
    float w;
    float h;
    float score;
    int type;
}
Cand;

typedef struct
{
    MediaNpuTensor info;
    int grid_w;
    int grid_h;
    int stride;
    int channels;
    const int *anchor;
}
Head;

struct AiDetect
{
    MediaNpu *npu;
    int model_w;
    int model_h;
    int n_head;
    Head head[3];
    void *buf[3];
    size_t bytes[3];
    Cand *cand;
    int cand_cap;
    BaseImage canvas;
    const BaseImage *frame;
    MediaPicPreprocess prep;
};

static int TypeOf(int coco)
{
    if (coco == kCocoCat)
    {
        return ALGO_CAT;
    }
    return -1;
}

static const int *AnchorsFor(int stride)
{
    if (stride <= 8)
    {
        return kAnchor8;
    }
    if (stride <= 16)
    {
        return kAnchor16;
    }
    return kAnchor32;
}

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

static int OutputGrid(const MediaNpuTensor *t, int *channels, int *height, int *width)
{
    if (t->fmt == MEDIA_NPU_FMT_NCHW && t->n_dims == 4)
    {
        *channels = t->dims[1];
        *height = t->dims[2];
        *width = t->dims[3];
        return 1;
    }
    if (t->fmt == MEDIA_NPU_FMT_NCHW && t->n_dims == 3)
    {
        *channels = t->dims[0];
        *height = t->dims[1];
        *width = t->dims[2];
        return 1;
    }
    if (t->fmt == MEDIA_NPU_FMT_NHWC && t->n_dims == 4)
    {
        *height = t->dims[1];
        *width = t->dims[2];
        *channels = t->dims[3];
        return 1;
    }
    if (t->fmt == MEDIA_NPU_FMT_NHWC && t->n_dims == 3)
    {
        *height = t->dims[0];
        *width = t->dims[1];
        *channels = t->dims[2];
        return 1;
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
        return q;
    }
    return 0.0f;
}

static float At(const void *base, const Head *head, int channel, int y, int x)
{
    int index;
    int grid = head->grid_w * head->grid_h;
    if (head->info.fmt == MEDIA_NPU_FMT_NHWC)
    {
        index = (y * head->grid_w + x) * head->channels + channel;
    }
    else
    {
        index = channel * grid + y * head->grid_w + x;
    }
    return Dequant(base, index, &head->info);
}

static int DecodeHead(const Head *head,
                      const void *data,
                      const int *want,
                      float score_min,
                      Cand *cand,
                      int cap,
                      int count)
{
    int anchor;
    int y;
    int x;
    for (anchor = 0; anchor < kAnchors; ++anchor)
    {
        int base = anchor * kPerAnchor;
        for (y = 0; y < head->grid_h; ++y)
        {
            for (x = 0; x < head->grid_w; ++x)
            {
                float obj;
                float best;
                int best_k;
                int k;
                float score;
                float bx;
                float by;
                float bw;
                float bh;
                if (count >= cap)
                {
                    return count;
                }
                obj = At(data, head, base + 4, y, x);
                best = 0.0f;
                best_k = -1;
                for (k = 0; k < kClassCount; ++k)
                {
                    float prob;
                    if (!want[k])
                    {
                        continue;
                    }
                    prob = At(data, head, base + 5 + k, y, x);
                    if (prob > best)
                    {
                        best = prob;
                        best_k = k;
                    }
                }
                if (best_k < 0)
                {
                    continue;
                }
                score = obj * best;
                if (score < score_min)
                {
                    continue;
                }
                bx = At(data, head, base + 0, y, x) * 2.0f - 0.5f;
                by = At(data, head, base + 1, y, x) * 2.0f - 0.5f;
                bw = At(data, head, base + 2, y, x) * 2.0f;
                bh = At(data, head, base + 3, y, x) * 2.0f;
                bx = (bx + (float)x) * (float)head->stride;
                by = (by + (float)y) * (float)head->stride;
                bw = bw * bw * (float)head->anchor[anchor * 2];
                bh = bh * bh * (float)head->anchor[anchor * 2 + 1];
                cand[count].x = bx - bw * 0.5f;
                cand[count].y = by - bh * 0.5f;
                cand[count].w = bw;
                cand[count].h = bh;
                cand[count].score = score;
                cand[count].type = TypeOf(best_k);
                count += 1;
            }
        }
    }
    return count;
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

static void Release(AiDetect *det)
{
    int i;
    if (det == NULL)
    {
        return;
    }
    for (i = 0; i < 3; ++i)
    {
        free(det->buf[i]);
    }
    free(det->cand);
    media_mem_free(&det->canvas);
    media_npu_close(det->npu);
    free(det);
}

BaseStatus ai_detect_open(const char *model_path, const AlgoOpenParam *param, AiDetect **out)
{
    AiDetect *det;
    MediaNpuTensor input;
    int count;
    int i;
    int cells;
    if (out != NULL)
    {
        *out = NULL;
    }
    if (model_path == NULL || out == NULL)
    {
        return BASE_ERR;
    }
    det = (AiDetect *)calloc(1, sizeof(*det));
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
    if (media_npu_input_info(det->npu, &input) != BASE_OK || !InputSize(&input, &det->model_w, &det->model_h))
    {
        Release(det);
        return BASE_ERR;
    }
    if (media_npu_output_count(det->npu, &count) != BASE_OK || count != 3)
    {
        Release(det);
        return BASE_ERR;
    }
    cells = 0;
    for (i = 0; i < 3; ++i)
    {
        Head *head = &det->head[i];
        int channels;
        int grid_h;
        int grid_w;
        if (media_npu_output_info(det->npu, i, &head->info) != BASE_OK ||
            !OutputGrid(&head->info, &channels, &grid_h, &grid_w))
        {
            Release(det);
            return BASE_ERR;
        }
        if (head->info.bytes == 0 || channels != kAnchors * kPerAnchor || grid_w < 2 || grid_h < 2 ||
            det->model_w % grid_w != 0 || det->model_h % grid_h != 0 ||
            det->model_w / grid_w != det->model_h / grid_h)
        {
            Release(det);
            return BASE_ERR;
        }
        head->channels = channels;
        head->grid_w = grid_w;
        head->grid_h = grid_h;
        head->stride = det->model_w / grid_w;
        head->anchor = AnchorsFor(head->stride);
        det->bytes[i] = head->info.bytes;
        det->buf[i] = malloc(det->bytes[i]);
        if (det->buf[i] == NULL)
        {
            Release(det);
            return BASE_ERR;
        }
        cells += kAnchors * grid_w * grid_h;
    }
    det->n_head = 3;
    det->cand = (Cand *)malloc((size_t)cells * sizeof(Cand));
    if (det->cand == NULL)
    {
        Release(det);
        return BASE_ERR;
    }
    det->cand_cap = cells;
    if (media_mem_alloc(det->model_w, det->model_h, BASE_PIX_RGB888, &det->canvas) != BASE_OK)
    {
        Release(det);
        return BASE_ERR;
    }
    *out = det;
    return BASE_OK;
}

void ai_detect_close(AiDetect *det)
{
    Release(det);
}

BaseStatus ai_detect_preprocess(AiDetect *det, const BaseImage *frame)
{
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

BaseStatus ai_detect_infer(AiDetect *det)
{
    void *outputs[3];
    int i;
    if (det == NULL || det->frame == NULL)
    {
        return BASE_ERR;
    }
    for (i = 0; i < det->n_head; ++i)
    {
        outputs[i] = det->buf[i];
    }
    return media_npu_run(det->npu, &det->canvas, outputs, det->bytes);
}

BaseStatus ai_detect_postprocess(AiDetect *det, AlgoBox *boxes, int capacity, int *count)
{
    int want[80];
    int i;
    int n;
    int keep;
    int written;
    if (count != NULL)
    {
        *count = 0;
    }
    if (det == NULL || det->frame == NULL || boxes == NULL || capacity <= 0 || count == NULL)
    {
        return BASE_ERR;
    }
    if (det->prep.content_w <= 0 || det->prep.content_h <= 0)
    {
        return BASE_ERR;
    }
    memset(want, 0, sizeof(want));
    want[kCocoCat] = 1;
    n = 0;
    for (i = 0; i < det->n_head; ++i)
    {
        n = DecodeHead(&det->head[i], det->buf[i], want, kScoreMin, det->cand, det->cand_cap, n);
    }
    if (n > 1)
    {
        qsort(det->cand, (size_t)n, sizeof(Cand), ScoreDesc);
    }
    keep = n;
    for (i = 0; i < n; ++i)
    {
        int j;
        if (det->cand[i].score < 0.0f)
        {
            continue;
        }
        for (j = i + 1; j < n; ++j)
        {
            if (det->cand[j].score < 0.0f || det->cand[j].type != det->cand[i].type)
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
    for (i = 0; i < keep && written < capacity; ++i)
    {
        BaseRect box;
        if (det->cand[i].score < kScoreMin)
        {
            continue;
        }
        if (!MapBox(&det->cand[i], det->frame, &det->prep, &box))
        {
            continue;
        }
        boxes[written].type = det->cand[i].type;
        boxes[written].score = det->cand[i].score;
        boxes[written].box = box;
        written += 1;
    }
    *count = written;
    return BASE_OK;
}

static BaseStatus DetectOpen(const char *model_path, const AlgoOpenParam *param, void **impl)
{
    return ai_detect_open(model_path, param, (AiDetect **)impl);
}

static void DetectClose(void *impl)
{
    ai_detect_close((AiDetect *)impl);
}

static BaseStatus DetectPre(void *impl, const BaseImage *frame)
{
    return ai_detect_preprocess((AiDetect *)impl, frame);
}

static BaseStatus DetectInfer(void *impl)
{
    return ai_detect_infer((AiDetect *)impl);
}

static BaseStatus DetectPost(void *impl, void *buf, int buf_size)
{
    AlgoDetectResult *result;
    result = (AlgoDetectResult *)buf;
    if (result == NULL || buf_size < (int)sizeof(*result))
    {
        return BASE_ERR;
    }
    return ai_detect_postprocess((AiDetect *)impl, result->box, ALGO_MAX, &result->count);
}

void ai_detect_register(void)
{
    static const AlgoModule mod =
    {
        ALGO_DETECT,
        "detect",
        (int)sizeof(AlgoDetectResult),
        DetectOpen,
        DetectClose,
        DetectPre,
        DetectInfer,
        DetectPost,
        NULL
    };
    algo_register_mod(&mod);
}
