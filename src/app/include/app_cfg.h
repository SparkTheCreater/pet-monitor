/**
 * @file    app_cfg.h
 * @brief   应用配置。相机、事件和发信各自一段。
 *
 * [camera] 是各路共用的超时。每一路是 [camera.名字]，place 取 eat 或 litter。
 * 最多 APP_CAMERA_MAX 路，按文件里的顺序。
 * [base] 只提供路径根。写错时返回 BASE_ERR，*out 不动。
 */
#ifndef APP_CFG_H
#define APP_CFG_H

#include "app_email.h"
#include "ini.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define APP_CAMERA_MAX 8
#define APP_NAME_MAX 32
#define APP_URL_MAX 512
#define APP_DIR_MAX 256
#define APP_HOST_MAX 128
#define APP_MAIL_MAX 128
#define APP_ADDR_MAX 256
#define APP_PATH_MAX 256

typedef enum
{
    APP_PLACE_EAT = 0,
    APP_PLACE_LITTER = 1
}
AppPlace;

typedef struct AppCamera
{
    char name[APP_NAME_MAX];
    char url[APP_URL_MAX];
    /* AppPlace。 */
    int place;
    int timeout_ms;
}
AppCamera;

typedef struct AppConfig
{
    char dir[APP_DIR_MAX];
    AppCamera camera[APP_CAMERA_MAX];
    int camera_count;
    int eat_sec;
    int litter_sec;
    int clip_sec;
    int email_enable;
    char host[APP_HOST_MAX];
    char user[APP_MAIL_MAX];
    char password[APP_MAIL_MAX];
    char from[APP_ADDR_MAX];
    char to[APP_ADDR_MAX];
    char ca[APP_PATH_MAX];
    /* 字符串指向本结构里的缓冲。ca 为空时 ca_file 为空。 */
    AppEmailServer email;
}
AppConfig;

BaseStatus app_cfg_from_ini(const BaseIni *ini, AppConfig *out);

#ifdef __cplusplus
}
#endif

#endif
