/**
 * @file    app_act.c
 * @brief   默认执行：为真才留图、落盘或发信。
 */
#define _POSIX_C_SOURCE 200809L

#include "app_act.h"

#include "app_cfg.h"
#include "app_mail.h"
#include "app_mark.h"
#include "media_jpeg.h"
#include "media_mem.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void Stamp(char *out, size_t size)
{
    struct tm tm;
    time_t sec;
    sec = time(NULL) + 8 * 3600;
    gmtime_r(&sec, &tm);
    snprintf(out, size, "%04d%02d%02d_%02d%02d%02d",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static const char *Tag(int pair)
{
    if (pair)
    {
        return "2cats";
    }
    return "event";
}

static void FileName(const char *camera, char *out, size_t size)
{
    size_t i;
    size_t n;
    n = 0;
    if (camera == NULL)
    {
        camera = "cam";
    }
    for (i = 0; camera[i] != '\0' && n + 1 < size; i++)
    {
        char c = camera[i];
        if (c == '/' || c == '\\')
        {
            c = '_';
        }
        out[n] = c;
        n += 1;
    }
    if (n == 0)
    {
        snprintf(out, size, "cam");
        return;
    }
    out[n] = '\0';
}

static void Save(const char *dir, const AppCamera *camera, int pair, const uint8_t *jpeg, size_t len)
{
    char name[APP_NAME_MAX];
    char stamp[32];
    char path[APP_PATH_MAX];
    FILE *fp;
    int wrote;
    if (dir == NULL || dir[0] == '\0' || camera == NULL || jpeg == NULL || len == 0)
    {
        return;
    }
    FileName(camera->name, name, sizeof(name));
    Stamp(stamp, sizeof(stamp));
    if (snprintf(path, sizeof(path), "%s/%s_%s_%s.jpg", dir, name, Tag(pair), stamp) >= (int)sizeof(path))
    {
        fprintf(stderr, "[app] %s save path\n", camera->name);
        return;
    }
    fp = fopen(path, "wb");
    if (fp == NULL)
    {
        fprintf(stderr, "[app] %s save open %s\n", camera->name, path);
        return;
    }
    wrote = fwrite(jpeg, 1, len, fp) == len;
    if (fclose(fp) != 0)
    {
        wrote = 0;
    }
    if (!wrote)
    {
        remove(path);
        fprintf(stderr, "[app] %s save write %s\n", camera->name, path);
        return;
    }
    fprintf(stderr, "[app] %s saved %s\n", camera->name, path);
}

void app_act_run(const AppShot *shot, const AppVerdict *verdict, void *user)
{
    const AppCamera *camera;
    uint8_t **jpeg;
    size_t *jpeg_len;
    (void)user;
    if (shot == NULL || verdict == NULL || shot->jpeg == NULL || shot->jpeg_len == NULL)
    {
        return;
    }
    jpeg = shot->jpeg;
    jpeg_len = shot->jpeg_len;
    camera = NULL;
    if (shot->fact != NULL)
    {
        camera = shot->fact->camera;
    }
    if (verdict->drop)
    {
        media_jpeg_free(*jpeg);
        *jpeg = NULL;
        *jpeg_len = 0;
    }
    if (verdict->keep && shot->frame != NULL && shot->fact != NULL && shot->fact->result != NULL)
    {
        BaseImage marked;
        uint8_t *encoded;
        size_t encoded_len;
        encoded = NULL;
        encoded_len = 0;
        memset(&marked, 0, sizeof(marked));
        marked.fd = -1;
        if (app_mark_cats(shot->frame, shot->fact->result, &marked) == BASE_OK &&
            media_jpeg_encode(&marked, shot->jpeg_quality, &encoded, &encoded_len) == BASE_OK)
        {
            media_jpeg_free(*jpeg);
            *jpeg = encoded;
            *jpeg_len = encoded_len;
            if (shot->kept != NULL)
            {
                shot->kept(shot->kept_user);
            }
        }
        media_mem_free(&marked);
    }
    if (verdict->send && *jpeg != NULL && camera != NULL)
    {
        Save(shot->dir, camera, verdict->pair, *jpeg, *jpeg_len);
        if (shot->mail != NULL)
        {
            fprintf(stderr, "[app] %s latched\n", camera->name);
            app_mail_post(shot->mail, camera->name, *jpeg, *jpeg_len);
            *jpeg = NULL;
            *jpeg_len = 0;
        }
    }
}
