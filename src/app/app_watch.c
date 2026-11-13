/**
 * @file    app_watch.c
 * @brief   出现、留图、发信、消失复位。时间都由调用方传入。
 */
#include "app_watch.h"

#include <string.h>

static int64_t SecMs(int sec)
{
    return (int64_t)sec * 1000;
}

void app_watch_reset(AppWatch *watch)
{
    if (watch == NULL)
    {
        return;
    }
    memset(watch, 0, sizeof(*watch));
    watch->gone_ms = -1;
}

void app_watch_kept(AppWatch *watch)
{
    if (watch != NULL && watch->phase == APP_WATCH_SEEN)
    {
        watch->held = 1;
    }
}

static void ToIdle(AppWatch *watch)
{
    watch->phase = APP_WATCH_IDLE;
    watch->held = 0;
    watch->mark_ms = 0;
    watch->gone_ms = -1;
}

void app_watch_step(AppWatch *watch, const AppWatchParam *param, int64_t now_ms, int has_cat, AppWatchEvent *ev)
{
    int64_t elapsed;
    if (ev != NULL)
    {
        memset(ev, 0, sizeof(*ev));
    }
    if (watch == NULL || param == NULL || ev == NULL)
    {
        return;
    }
    if (watch->phase == APP_WATCH_IDLE)
    {
        if (!has_cat)
        {
            return;
        }
        watch->phase = APP_WATCH_SEEN;
        watch->mark_ms = now_ms;
        watch->held = 0;
        watch->gone_ms = -1;
        return;
    }
    if (watch->phase == APP_WATCH_SENT)
    {
        if (has_cat)
        {
            watch->gone_ms = -1;
            return;
        }
        if (watch->gone_ms < 0)
        {
            watch->gone_ms = now_ms;
        }
        if (now_ms - watch->gone_ms >= SecMs(param->absent_sec))
        {
            ToIdle(watch);
        }
        return;
    }
    if (!has_cat)
    {
        if (watch->held)
        {
            ev->drop = 1;
        }
        ToIdle(watch);
        return;
    }
    elapsed = now_ms - watch->mark_ms;
    if (!watch->held && elapsed >= SecMs(param->shot_sec) && elapsed < SecMs(param->present_sec))
    {
        ev->keep = 1;
        return;
    }
    if (elapsed < SecMs(param->present_sec))
    {
        return;
    }
    if (watch->held)
    {
        watch->phase = APP_WATCH_SENT;
        watch->gone_ms = -1;
        ev->send = 1;
        return;
    }
    ToIdle(watch);
}
