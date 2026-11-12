/**
 * @file    media_ai_probe.c
 * @brief   给 media_ai_load 用的最小插件。种类号 80。
 */
#include "media_ai.h"

#include <stdlib.h>

static BaseStatus Open(const char *model_path, const MediaAiOpenParam *param, void **impl)
{
    (void)model_path;
    (void)param;
    if (impl == NULL)
    {
        return BASE_ERR;
    }
    *impl = calloc(1, sizeof(int));
    if (*impl == NULL)
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

static void Close(void *impl)
{
    free(impl);
}

static BaseStatus Pre(void *impl, const BaseImage *frame)
{
    (void)impl;
    if (!base_image_valid(frame))
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

static BaseStatus Infer(void *impl)
{
    (void)impl;
    return BASE_OK;
}

static BaseStatus Post(void *impl, void *buf, int buf_size)
{
    (void)impl;
    if (buf == NULL || buf_size < (int)sizeof(int))
    {
        return BASE_ERR;
    }
    *(int *)buf = 42;
    return BASE_OK;
}

static const MediaAiModule g_mod =
{
    80,
    "probe",
    (int)sizeof(int),
    Open,
    Close,
    Pre,
    Infer,
    Post,
    NULL
};

const MediaAiModule *media_ai_module(void)
{
    return &g_mod;
}
