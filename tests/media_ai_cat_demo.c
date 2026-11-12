#define _POSIX_C_SOURCE 200809L

/**
 * @file    media_ai_cat_demo.c
 * @brief   用一张 JPEG 走一遍猫检测插件。默认模型是 models/Catto.rknn。
 */
#include "media_ai.h"
#include "media_jpeg.h"
#include "media_mem.h"
#include "media_sys.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int done;
    int cb_type;
    int status;
    MediaAiDetectResult result;
}
Wait;

static void OnDetect(const MediaAiCallbackInfo *info)
{
    Wait *wait;
    wait = (Wait *)info->user;
    pthread_mutex_lock(&wait->mu);
    wait->cb_type = info->cb_type;
    wait->status = info->status;
    memset(&wait->result, 0, sizeof(wait->result));
    if (info->cb_type == MEDIA_AI_CB_DATA &&
        info->buf != NULL &&
        info->buf_size >= (int)sizeof(MediaAiDetectResult))
    {
        memcpy(&wait->result, info->buf, sizeof(wait->result));
    }
    wait->done = 1;
    pthread_cond_signal(&wait->cv);
    pthread_mutex_unlock(&wait->mu);
}

static int WaitDone(Wait *wait)
{
    struct timespec ts;
    int ok;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 10;
    ok = 1;
    pthread_mutex_lock(&wait->mu);
    while (!wait->done)
    {
        if (pthread_cond_timedwait(&wait->cv, &wait->mu, &ts) != 0)
        {
            ok = 0;
            break;
        }
    }
    pthread_mutex_unlock(&wait->mu);
    return ok;
}

static unsigned char *ReadFile(const char *path, size_t *size)
{
    FILE *fp;
    long n;
    unsigned char *data;
    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return NULL;
    }
    n = ftell(fp);
    if (n <= 0 || fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return NULL;
    }
    data = (unsigned char *)malloc((size_t)n);
    if (data == NULL || fread(data, 1, (size_t)n, fp) != (size_t)n)
    {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size = (size_t)n;
    return data;
}

static void PrintCats(const BaseImage *image, const MediaAiDetectResult *result)
{
    int i;
    printf("detect count=%d\n", result->count);
    for (i = 0; i < result->count; ++i)
    {
        const MediaAiBox *box = &result->box[i];
        float cx;
        float cy;
        cx = ((float)box->box.x + (float)box->box.w * 0.5f) / (float)image->width;
        cy = ((float)box->box.y + (float)box->box.h * 0.5f) / (float)image->height;
        printf("cat type=%d score=%.3f box=(%d %d %d %d) rel=(%.3f %.3f)\n",
               box->type,
               box->score,
               box->box.x,
               box->box.y,
               box->box.w,
               box->box.h,
               cx,
               cy);
    }
}

int main(int argc, char **argv)
{
    const char *image_path = argc > 1 ? argv[1] : "tests/data/cat-1080p.jpg";
    const char *model_path = argc > 2 ? argv[2] : "models/Catto.rknn";
    unsigned char *jpeg = NULL;
    size_t jpeg_size = 0;
    BaseImage image;
    MediaAi *ai = NULL;
    MediaAiInput in;
    Wait wait;
    int loaded = 0;
    int code = 1;

    memset(&image, 0, sizeof(image));
    memset(&wait, 0, sizeof(wait));
    pthread_mutex_init(&wait.mu, NULL);
    pthread_cond_init(&wait.cv, NULL);

    if (media_init() != BASE_OK)
    {
        fprintf(stderr, "media_init failed\n");
        goto out;
    }
    jpeg = ReadFile(image_path, &jpeg_size);
    if (jpeg == NULL)
    {
        fprintf(stderr, "read image failed: %s\n", image_path);
        goto out;
    }
    if (media_jpeg_decode(jpeg, jpeg_size, &image) != BASE_OK)
    {
        fprintf(stderr, "jpeg decode failed\n");
        goto out;
    }
    printf("frame %dx%d format=%d\n", image.width, image.height, image.format);
    if (image.width < 2 || image.height < 2)
    {
        fprintf(stderr, "image is too small\n");
        goto out;
    }
    if (media_ai_load(MEDIA_AI_CAT_DET_PLUGIN) != BASE_OK)
    {
        fprintf(stderr, "media_ai_load failed\n");
        goto out;
    }
    loaded = 1;
    if (media_ai_open(NULL, &ai) != BASE_OK)
    {
        fprintf(stderr, "media_ai_open failed\n");
        goto out;
    }
    if (media_ai_add(ai, MEDIA_AI_CAT_DET, model_path) != BASE_OK)
    {
        fprintf(stderr, "media_ai_add failed: %s\n", model_path);
        goto out;
    }
    if (media_ai_set_callback(ai, MEDIA_AI_CAT_DET, OnDetect, &wait) != BASE_OK)
    {
        fprintf(stderr, "media_ai_set_callback failed\n");
        goto out;
    }

    memset(&in, 0, sizeof(in));
    in.type = MEDIA_AI_DATA_FRAME;
    in.frame = &image;
    in.exec = MEDIA_AI_EXEC_SERIAL;
    if (media_ai_input(ai, &in) != BASE_OK)
    {
        fprintf(stderr, "media_ai_input failed\n");
        goto out;
    }
    if (!WaitDone(&wait))
    {
        fprintf(stderr, "callback timeout\n");
        goto out;
    }
    if (wait.cb_type != MEDIA_AI_CB_DATA || wait.status != BASE_OK)
    {
        fprintf(stderr, "callback type=%d status=%d\n", wait.cb_type, wait.status);
        goto out;
    }
    PrintCats(&image, &wait.result);
    code = wait.result.count > 0 ? 0 : 2;

out:
    media_ai_close(ai);
    if (loaded)
    {
        media_ai_unload(MEDIA_AI_CAT_DET_PLUGIN);
    }
    media_mem_free(&image);
    free(jpeg);
    media_deinit();
    pthread_cond_destroy(&wait.cv);
    pthread_mutex_destroy(&wait.mu);
    return code;
}
