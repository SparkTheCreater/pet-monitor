/**
 * @file    net_cfg.h
 * @brief   从 [camera] 和 [camera.*] 填拉流地址。不读 place，也不连接。
 *
 * [camera] 是各路共用的超时和传输方式。每一路是 [camera.名字]，必须有 rtsp url。
 * 最多 NET_SOURCE_MAX 路，按文件里的顺序。名字和地址的长度与应用层相机记录对齐，
 * 以后按名字把一路源和 place 对上。
 * 写错时返回 BASE_ERR，*out 不动。
 */
#ifndef NET_CFG_H
#define NET_CFG_H

#include "ini.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 与 APP_CAMERA_MAX、APP_NAME_MAX、APP_URL_MAX 相同。 */
#define NET_SOURCE_MAX 8
#define NET_NAME_MAX 32
#define NET_URL_MAX 512

typedef enum
{
    NET_TRANSPORT_TCP = 0,
    NET_TRANSPORT_UDP = 1
}
NetTransport;

typedef struct NetSource
{
    char name[NET_NAME_MAX];
    char url[NET_URL_MAX];
    int timeout_ms;
}
NetSource;

typedef struct NetConfig
{
    NetSource source[NET_SOURCE_MAX];
    int source_count;
    /* NetTransport。各路共用。 */
    int transport;
}
NetConfig;

BaseStatus net_cfg_from_ini(const BaseIni *ini, NetConfig *out);

#ifdef __cplusplus
}
#endif

#endif
