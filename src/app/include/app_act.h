/**
 * @file    app_act.h
 * @brief   判断为真之后做的那一步。不决定要不要做。
 *
 * 和判断函数一样，换实现就换这一整个函数。默认是 app_act_run：
 * 先丢掉，再画框编 JPEG，最后交给邮件。为假的那一步跳过。
 */
#ifndef APP_ACT_H
#define APP_ACT_H

#include "app_judge.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct AppMail;

typedef struct AppShot
{
    const AppFact *fact;
    BaseImage *frame;
    uint8_t **jpeg;
    size_t *jpeg_len;
    int jpeg_quality;
    AppJudgeKeptFn kept;
    void *kept_user;
    struct AppMail *mail;
}
AppShot;

typedef void (*AppActFn)(const AppShot *shot, const AppVerdict *verdict, void *user);

/* user 不用。邮件用 shot->mail，空则发信这一步不拿走 JPEG。 */
void app_act_run(const AppShot *shot, const AppVerdict *verdict, void *user);

#ifdef __cplusplus
}
#endif

#endif
