/**
 * @file    app_cfg.h
 * @brief   应用配置。相机、事件和发信各自一段。
 *
 * [camera] 是各路共用的超时。每一路是 [camera.名字]，place 取 eat、litter、客厅、阳台。
 * 每一路写 algo 和 score_min。algo 取 detect、track、recognize、cat。
 * 缺省 algo 是 cat，缺省 score_min 用 [algo] 的 score_min。
 * 每一路可以写 roi = x,y,w,h，单位是原图像素。没写就看整幅画面。
 * 运行时用 app_cfg_camera_algo、app_cfg_camera_score，框是否落在区域内用 app_cfg_box_inside。
 * 最多 APP_CAMERA_MAX 路，按文件里的顺序。
 * [base] 只提供路径根。写错时返回 BASE_ERR，*out 不动。
 */
#ifndef APP_CFG_H
#define APP_CFG_H

#include "algo.h"
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
    APP_PLACE_LITTER = 1,
    APP_PLACE_LIVING = 2,
    APP_PLACE_BALCONY = 3
}
AppPlace;

typedef struct AppCamera
{
    char name[APP_NAME_MAX];
    char url[APP_URL_MAX];
    /* AppPlace。 */
    int place;
    int timeout_ms;
    /* AlgoKind。用 app_cfg_camera_algo 读取。 */
    int algo;
    /* 0 到 1。用 app_cfg_camera_score 读取。 */
    float score_min;
    /* 1 表示 roi_* 有效，坐标是原图像素。 */
    int has_roi;
    int roi_x;
    int roi_y;
    int roi_w;
    int roi_h;
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
    /* 猫要连续出现这么久才发信，发出去的是出现后 shot_sec 那一张。 */
    int present_sec;
    int shot_sec;
    /* 两只及以上要连续同时出现这么久才发当前这一帧。 */
    int pair_sec;
    /* 发出去之后，这么久以内不再发。结束时重新计。 */
    int cooldown_sec;
    /* 旧配置仍读入。再发间隔以 cooldown_sec 为准。 */
    int absent_sec;
    /* 判真时 JPEG 写到这里。已经拼成绝对路径。 */
    char shot_dir[APP_PATH_MAX];
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

int app_cfg_camera_algo(const AppCamera *camera);

float app_cfg_camera_score(const AppCamera *camera);

/* 没配区域时任意框都算在内。配了区域时看框中心。 */
int app_cfg_box_inside(const AppCamera *camera, const BaseRect *box);

#ifdef __cplusplus
}
#endif

#endif
