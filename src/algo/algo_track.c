/**
 * @file    algo_track.c
 * @brief   追踪算法。复用检测框，在句柄里把同一目标串成编号。
 */
#include "algo_internal.h"

#include <stdlib.h>
#include <string.h>

static const float kMatchIou = 0.3f;

typedef struct
{
    int id;
    int type;
    BaseRect box;
}
Prev;

struct AiTrack
{
    AiDetect *detect;
    Prev prev[ALGO_MAX];
    int n_prev;
    int next_id;
};

static float Iou(const BaseRect *a, const BaseRect *b)
{
    int ax2 = a->x + a->w;
    int ay2 = a->y + a->h;
    int bx2 = b->x + b->w;
    int by2 = b->y + b->h;
    int x1 = a->x > b->x ? a->x : b->x;
    int y1 = a->y > b->y ? a->y : b->y;
    int x2 = ax2 < bx2 ? ax2 : bx2;
    int y2 = ay2 < by2 ? ay2 : by2;
    int iw = x2 - x1;
    int ih = y2 - y1;
    float inter;
    float uni;
    if (iw <= 0 || ih <= 0 || a->w <= 0 || a->h <= 0 || b->w <= 0 || b->h <= 0)
    {
        return 0.0f;
    }
    inter = (float)iw * (float)ih;
    uni = (float)a->w * (float)a->h + (float)b->w * (float)b->h - inter;
    if (uni <= 0.0f)
    {
        return 0.0f;
    }
    return inter / uni;
}

BaseStatus ai_track_open(const char *model_path, const AlgoOpenParam *param, AiTrack **out)
{
    AiTrack *track;
    if (out != NULL)
    {
        *out = NULL;
    }
    if (model_path == NULL || out == NULL)
    {
        return BASE_ERR;
    }
    track = (AiTrack *)calloc(1, sizeof(*track));
    if (track == NULL)
    {
        return BASE_ERR;
    }
    track->next_id = 1;
    if (ai_detect_open(model_path, param, &track->detect) != BASE_OK)
    {
        free(track);
        return BASE_ERR;
    }
    *out = track;
    return BASE_OK;
}

void ai_track_close(AiTrack *track)
{
    if (track == NULL)
    {
        return;
    }
    ai_detect_close(track->detect);
    free(track);
}

BaseStatus ai_track_preprocess(AiTrack *track, const BaseImage *frame)
{
    if (track == NULL)
    {
        return BASE_ERR;
    }
    return ai_detect_preprocess(track->detect, frame);
}

BaseStatus ai_track_infer(AiTrack *track)
{
    if (track == NULL)
    {
        return BASE_ERR;
    }
    return ai_detect_infer(track->detect);
}

BaseStatus ai_track_postprocess(AiTrack *track, AlgoTrackResult *result)
{
    AlgoBox boxes[ALGO_MAX];
    int n;
    int i;
    int used[ALGO_MAX];
    int written;
    int capacity;
    Prev next[ALGO_MAX];
    if (track == NULL || result == NULL)
    {
        return BASE_ERR;
    }
    result->count = 0;
    if (ai_detect_postprocess(track->detect, boxes, ALGO_MAX, &n) != BASE_OK)
    {
        return BASE_ERR;
    }
    capacity = ALGO_MAX;
    memset(used, 0, sizeof(used));
    written = 0;
    for (i = 0; i < n && written < capacity && written < ALGO_MAX; ++i)
    {
        int j;
        int best;
        float best_iou;
        best = -1;
        best_iou = kMatchIou;
        for (j = 0; j < track->n_prev; ++j)
        {
            float iou;
            if (used[j] || track->prev[j].type != boxes[i].type)
            {
                continue;
            }
            iou = Iou(&track->prev[j].box, &boxes[i].box);
            if (iou >= best_iou)
            {
                best_iou = iou;
                best = j;
            }
        }
        if (best >= 0)
        {
            used[best] = 1;
            result->item[written].id = track->prev[best].id;
        }
        else
        {
            result->item[written].id = track->next_id;
            track->next_id += 1;
            if (track->next_id <= 0)
            {
                track->next_id = 1;
            }
        }
        result->item[written].type = boxes[i].type;
        result->item[written].score = boxes[i].score;
        result->item[written].box = boxes[i].box;
        next[written].id = result->item[written].id;
        next[written].type = result->item[written].type;
        next[written].box = result->item[written].box;
        written += 1;
    }
    memcpy(track->prev, next, (size_t)written * sizeof(Prev));
    track->n_prev = written;
    result->count = written;
    return BASE_OK;
}

static BaseStatus TrackOpen(const char *model_path, const AlgoOpenParam *param, void **impl)
{
    return ai_track_open(model_path, param, (AiTrack **)impl);
}

static void TrackClose(void *impl)
{
    ai_track_close((AiTrack *)impl);
}

static BaseStatus TrackPre(void *impl, const BaseImage *frame)
{
    return ai_track_preprocess((AiTrack *)impl, frame);
}

static BaseStatus TrackInfer(void *impl)
{
    return ai_track_infer((AiTrack *)impl);
}

static BaseStatus TrackPost(void *impl, void *buf, int buf_size)
{
    if (buf == NULL || buf_size < (int)sizeof(AlgoTrackResult))
    {
        return BASE_ERR;
    }
    return ai_track_postprocess((AiTrack *)impl, (AlgoTrackResult *)buf);
}

void ai_track_register(void)
{
    static const AlgoModule mod =
    {
        ALGO_TRACK,
        "track",
        (int)sizeof(AlgoTrackResult),
        TrackOpen,
        TrackClose,
        TrackPre,
        TrackInfer,
        TrackPost,
        NULL
    };
    algo_register_mod(&mod);
}
