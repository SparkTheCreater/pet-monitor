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
    watch->pair_ms = -1;
    watch->roi_ms = -1;
}

void app_watch_kept(AppWatch *watch)
{
    if (watch == NULL || watch->phase != APP_WATCH_SEEN)
    {
        return;
    }
    watch->held = 1;
    if (watch->pending)
    {
        watch->phase = APP_WATCH_SENT;
        watch->pending = 0;
    }
}

void app_watch_judge(const AppFact *fact, void *user, AppVerdict *out)
{
    AppWatchJudge *judge;
    AppWatchEvent ev;
    if (out == NULL)
    {
        return;
    }
    memset(out, 0, sizeof(*out));
    judge = user;
    if (fact == NULL || judge == NULL)
    {
        return;
    }
    app_watch_step(&judge->watch, &judge->param, fact->now_ms, fact->cats, fact->roi_cats, &ev);
    out->keep = ev.keep;
    out->drop = ev.drop;
    out->send = ev.send;
    out->pair = ev.pair;
}

void app_watch_judge_kept(void *user)
{
    AppWatchJudge *judge = user;
    if (judge == NULL)
    {
        return;
    }
    app_watch_kept(&judge->watch);
}

void app_watch_judge_reset(void *user)
{
    AppWatchJudge *judge = user;
    if (judge == NULL)
    {
        return;
    }
    app_watch_reset(&judge->watch);
}

static void ToIdle(AppWatch *watch)
{
    watch->phase = APP_WATCH_IDLE;
    watch->held = 0;
    watch->pending = 0;
    watch->roi_ms = -1;
    watch->pair_ms = -1;
    watch->cool_ms = 0;
}

static void ArmPair(AppWatch *watch, int64_t now_ms, AppWatchEvent *ev)
{
    if (watch->phase != APP_WATCH_SEEN)
    {
        watch->phase = APP_WATCH_SEEN;
        watch->held = 0;
        watch->roi_ms = -1;
    }
    watch->pending = 1;
    watch->cool_ms = now_ms;
    ev->keep = 1;
    ev->send = 1;
    ev->pair = 1;
}

void app_watch_step(AppWatch *watch, const AppWatchParam *param, int64_t now_ms, int frame_cats, int roi_cats, AppWatchEvent *ev)
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
    if (frame_cats < 0)
    {
        frame_cats = 0;
    }
    if (roi_cats < 0)
    {
        roi_cats = 0;
    }
    if (roi_cats > frame_cats)
    {
        roi_cats = frame_cats;
    }
    if (watch->phase == APP_WATCH_SENT)
    {
        if (now_ms - watch->cool_ms < SecMs(param->cooldown_sec))
        {
            return;
        }
        ToIdle(watch);
    }
    if (frame_cats >= 2)
    {
        if (watch->pair_ms < 0)
        {
            watch->pair_ms = now_ms;
        }
        if (now_ms - watch->pair_ms >= SecMs(param->pair_sec))
        {
            ArmPair(watch, now_ms, ev);
            return;
        }
    }
    else
    {
        watch->pair_ms = -1;
    }
    watch->pending = 0;
    if (roi_cats <= 0)
    {
        if (watch->phase == APP_WATCH_SEEN && watch->held)
        {
            ev->drop = 1;
        }
        watch->held = 0;
        watch->roi_ms = -1;
        if (watch->pair_ms < 0)
        {
            if (watch->phase != APP_WATCH_IDLE)
            {
                ToIdle(watch);
            }
            return;
        }
        if (watch->phase == APP_WATCH_IDLE)
        {
            watch->phase = APP_WATCH_SEEN;
        }
        return;
    }
    if (watch->phase == APP_WATCH_IDLE)
    {
        watch->phase = APP_WATCH_SEEN;
        watch->held = 0;
        watch->pending = 0;
    }
    if (watch->roi_ms < 0)
    {
        watch->roi_ms = now_ms;
    }
    elapsed = now_ms - watch->roi_ms;
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
        watch->cool_ms = now_ms;
        ev->send = 1;
        return;
    }
    watch->roi_ms = -1;
    watch->held = 0;
    if (watch->pair_ms < 0)
    {
        ToIdle(watch);
    }
}
