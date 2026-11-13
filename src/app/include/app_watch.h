/**
 * @file    app_watch.h
 * @brief   一路相机上的猫：出现够久发一次，消失够久才允许下一次。
 *
 * 不读图，也不发信。调用方按事件自己留 JPEG、丢掉或交出去。
 * now_ms 用单调时钟。有猫是这一拍的结论，中间没采样到的时间不算中断。
 */
#ifndef APP_WATCH_H
#define APP_WATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    APP_WATCH_IDLE = 0,
    APP_WATCH_SEEN = 1,
    APP_WATCH_SENT = 2
};

typedef struct AppWatchParam
{
    int present_sec;
    int shot_sec;
    int absent_sec;
}
AppWatchParam;

typedef struct AppWatch
{
    int phase;
    /* 已经留下 shot_sec 那一张。 */
    int held;
    int64_t mark_ms;
    int64_t gone_ms;
}
AppWatch;

typedef struct AppWatchEvent
{
    /* 这一拍的图要编成 JPEG 留下来。 */
    int keep;
    /* 留下的 JPEG 作废。 */
    int drop;
    /* 把留下的 JPEG 发出去。相位已经是已发送。 */
    int send;
}
AppWatchEvent;

void app_watch_reset(AppWatch *watch);

/*
 * 推进一步。watch 和 param 不可空。
 * keep 之后要调 app_watch_kept，编码失败就不要调，这一轮不会发信。
 */
void app_watch_step(AppWatch *watch, const AppWatchParam *param, int64_t now_ms, int has_cat, AppWatchEvent *ev);

void app_watch_kept(AppWatch *watch);

#ifdef __cplusplus
}
#endif

#endif
