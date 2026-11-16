/**
 * @file    app_act.c
 * @brief   默认执行：为真才留图或发信。
 */
#include "app_act.h"

#include "app_cfg.h"
#include "app_mail.h"
#include "app_mark.h"
#include "media_jpeg.h"
#include "media_mem.h"

#include <stdio.h>
#include <string.h>

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
    if (verdict->send && *jpeg != NULL && shot->mail != NULL && camera != NULL)
    {
        fprintf(stderr, "[app] %s latched\n", camera->name);
        app_mail_post(shot->mail, camera->name, *jpeg, *jpeg_len);
        *jpeg = NULL;
        *jpeg_len = 0;
    }
}
