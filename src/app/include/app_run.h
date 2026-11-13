/**
 * @file    app_run.h
 * @brief   按 ini 把每路相机跑起来：拉流、解码、猫检测、到点发信。
 *
 * 阻塞到收到 SIGINT 或 SIGTERM。每路一个线程，邮件在另一个线程。
 */
#ifndef APP_RUN_H
#define APP_RUN_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

BaseStatus app_run(const char *ini_path);

#ifdef __cplusplus
}
#endif

#endif
