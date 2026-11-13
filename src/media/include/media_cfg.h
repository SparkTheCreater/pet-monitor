/**
 * @file    media_cfg.h
 * @brief   从 [media] 填编码参数和 JPEG 质量。
 *
 * 缺键时用和 conf/pet-monitor.ini 相同的默认值。
 * 写错或越界返回 BASE_ERR，*out 不动。
 */
#ifndef MEDIA_CFG_H
#define MEDIA_CFG_H

#include "ini.h"
#include "media_enc.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct MediaConfig
{
    MediaEncParam enc;
    int jpeg_quality;
}
MediaConfig;

BaseStatus media_cfg_from_ini(const BaseIni *ini, MediaConfig *out);

#ifdef __cplusplus
}
#endif

#endif
