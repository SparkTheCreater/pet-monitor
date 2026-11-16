/**
 * @file    app_watch.h
 * @brief   一路相机上的猫。一只停留够久，或两只连续同时出现够久，发一次。
 *
 * 不读图，也不发信。app_watch_judge 是默认判断函数，状态放在 user 里。
 * 两只及以上看全画面，要连续满 pair_sec。中间掉成一只或没有，只清两只的计时。
 * 一只猫看检测区域。区域内没有猫时，清掉一只的计时，并丢掉已留的图。
 * 两条计时互不相消。交出去之后，画面里没有猫连续满 absent_sec 才允许下一次。
 * now_ms 用单调时钟。中间没采样到的时间不算中断。
 */
#ifndef APP_WATCH_H
#define APP_WATCH_H

#include "app_judge.h"

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
    int pair_sec;
    int absent_sec;
}
AppWatchParam;

typedef struct AppWatch
{
    int phase;
    /* 已经留下 shot_sec 那一张。 */
    int held;
    /* 这一次留下图之后就进入冷却。两只猫满 pair_sec 时用。 */
    int pending;
    /* 区域内这只猫的起点。小于 0 表示区域内当前没有猫。 */
    int64_t roi_ms;
    /* 这一轮两只猫连续出现的起点。小于 0 表示当前不满两只。 */
    int64_t pair_ms;
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
    /* 这一次由全画面的两只猫规则触发。 */
    int pair;
}
AppWatchEvent;

void app_watch_reset(AppWatch *watch);

/*
 * 推进一步。frame_cats 是全画面的猫数，roi_cats 是检测区域内的猫数。
 * watch 和 param 不可空。
 * keep 之后要调 app_watch_kept，编码失败就不要调，这一轮不会发信。
 */
void app_watch_step(AppWatch *watch, const AppWatchParam *param, int64_t now_ms, int frame_cats, int roi_cats, AppWatchEvent *ev);

void app_watch_kept(AppWatch *watch);

typedef struct AppWatchJudge
{
    AppWatch watch;
    AppWatchParam param;
}
AppWatchJudge;

void app_watch_judge(const AppFact *fact, void *user, AppVerdict *out);

void app_watch_judge_kept(void *user);

void app_watch_judge_reset(void *user);

#ifdef __cplusplus
}
#endif

#endif
