/**
 * @file    app_run.h
 * @brief   按 ini 把每路相机跑起来：拉流、解码、判断、到点把图交出去。
 *
 * 默认判断是猫连续出现够久发一次。要换规则，在 app_run 之前调用 app_run_set_judge。
 * 要换成功后的执行，调用 app_run_set_act。空表示恢复默认。
 * 阻塞到收到 SIGINT 或 SIGTERM。每路一个线程，邮件在另一个线程。
 */
#ifndef APP_RUN_H
#define APP_RUN_H

#include "app_act.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct AppRunJudge
{
    AppJudgeFn judge;
    AppJudgeKeptFn kept;
    /* 每次重新拉流时调用。空表示不额外清状态。 */
    void (*reset)(void *user);
    /* 每路相机一份 user。返回的指针要活到 app_run 结束。 */
    void *(*open)(int index, const struct AppCamera *camera, void *user);
    void *user;
}
AppRunJudge;

typedef struct AppRunAct
{
    AppActFn act;
    void *user;
}
AppRunAct;

/* 空或者 judge 为空，恢复成停留计时。 */
void app_run_set_judge(const AppRunJudge *judge);

/* 空或者 act 为空，恢复成画框、编码、发邮件。 */
void app_run_set_act(const AppRunAct *act);

BaseStatus app_run(const char *ini_path);

#ifdef __cplusplus
}
#endif

#endif
