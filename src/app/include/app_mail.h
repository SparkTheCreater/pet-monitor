/**
 * @file    app_mail.h
 * @brief   把编好的 JPEG 交给后台去发。调用返回时信还没送出。
 *
 * jpeg 的所有权交进来。没开邮件、队列满或正在关闭时，在这里释放。
 * cfg 要活得比这个句柄久。
 */
#ifndef APP_MAIL_H
#define APP_MAIL_H

#include "app_cfg.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct AppMail AppMail;

BaseStatus app_mail_open(const AppConfig *cfg, AppMail **out);

/* 同步。等已经收下的信发完再退出。 */
void app_mail_close(AppMail *mail);

BaseStatus app_mail_post(AppMail *mail, const char *camera, uint8_t *jpeg, size_t len);

#ifdef __cplusplus
}
#endif

#endif
