/**
 * @file    media_cfg.c
 * @brief   把 [media] 译成 MediaEncParam。打开编码器的检查仍在 media_enc_open。
 */
#include "media_cfg.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static char Lower(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int Eq(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
    {
        return 0;
    }
    while (*a != '\0' && Lower(*a) == Lower(*b))
    {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static BaseStatus CodecOf(const char *text, int *out)
{
    if (Eq(text, "h264"))
    {
        *out = BASE_CODEC_H264;
        return BASE_OK;
    }
    if (Eq(text, "h265"))
    {
        *out = BASE_CODEC_H265;
        return BASE_OK;
    }
    return BASE_ERR;
}

static BaseStatus FormatOf(const char *text, int *out)
{
    if (Eq(text, "nv12"))
    {
        *out = BASE_PIX_NV12;
        return BASE_OK;
    }
    if (Eq(text, "rgb888"))
    {
        *out = BASE_PIX_RGB888;
        return BASE_OK;
    }
    return BASE_ERR;
}

static BaseStatus RcOf(const char *text, int *out)
{
    if (Eq(text, "cbr"))
    {
        *out = MEDIA_RC_CBR;
        return BASE_OK;
    }
    if (Eq(text, "vbr"))
    {
        *out = MEDIA_RC_VBR;
        return BASE_OK;
    }
    return BASE_ERR;
}

static BaseStatus ProfileOf(const char *text, int *out)
{
    char *end;
    long v;
    if (Eq(text, "baseline"))
    {
        *out = 66;
        return BASE_OK;
    }
    if (Eq(text, "main"))
    {
        *out = 77;
        return BASE_OK;
    }
    if (Eq(text, "high"))
    {
        *out = 100;
        return BASE_OK;
    }
    if (text == NULL)
    {
        return BASE_ERR;
    }
    errno = 0;
    v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX)
    {
        return BASE_ERR;
    }
    *out = (int)v;
    return BASE_OK;
}

static int Valid(const MediaConfig *cfg)
{
    const MediaEncParam *param = &cfg->enc;
    if (param->codec != BASE_CODEC_H264 && param->codec != BASE_CODEC_H265)
    {
        return 0;
    }
    if (param->format != BASE_PIX_NV12 && param->format != BASE_PIX_RGB888)
    {
        return 0;
    }
    if (param->width <= 0 || param->height <= 0 || param->fps <= 0 || param->gop <= 0 || param->bitrate <= 0)
    {
        return 0;
    }
    if (param->rc_mode != MEDIA_RC_CBR && param->rc_mode != MEDIA_RC_VBR)
    {
        return 0;
    }
    if (param->qp_min < 0 || param->qp_max > 51 || param->qp_min > param->qp_init || param->qp_init > param->qp_max)
    {
        return 0;
    }
    if (param->codec == BASE_CODEC_H264 &&
        (param->profile != 66 && param->profile != 77 && param->profile != 100))
    {
        return 0;
    }
    if (param->codec == BASE_CODEC_H264 && (param->level < 10 || param->level > 52))
    {
        return 0;
    }
    if (cfg->jpeg_quality < 1 || cfg->jpeg_quality > 100)
    {
        return 0;
    }
    return 1;
}

static BaseStatus Read(const BaseIni *ini, MediaConfig *cfg)
{
    if (CodecOf(base_ini_str(ini, "media", "codec", "h264"), &cfg->enc.codec) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (FormatOf(base_ini_str(ini, "media", "format", "nv12"), &cfg->enc.format) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (RcOf(base_ini_str(ini, "media", "rc", "cbr"), &cfg->enc.rc_mode) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (ProfileOf(base_ini_str(ini, "media", "profile", "high"), &cfg->enc.profile) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (base_ini_int(ini, "media", "width", 1920, &cfg->enc.width) != BASE_OK ||
        base_ini_int(ini, "media", "height", 1080, &cfg->enc.height) != BASE_OK ||
        base_ini_int(ini, "media", "fps", 25, &cfg->enc.fps) != BASE_OK ||
        base_ini_int(ini, "media", "gop", 50, &cfg->enc.gop) != BASE_OK ||
        base_ini_int(ini, "media", "bitrate", 2000000, &cfg->enc.bitrate) != BASE_OK ||
        base_ini_int(ini, "media", "qp_init", 26, &cfg->enc.qp_init) != BASE_OK ||
        base_ini_int(ini, "media", "qp_min", 10, &cfg->enc.qp_min) != BASE_OK ||
        base_ini_int(ini, "media", "qp_max", 51, &cfg->enc.qp_max) != BASE_OK ||
        base_ini_int(ini, "media", "level", 40, &cfg->enc.level) != BASE_OK ||
        base_ini_int(ini, "media", "jpeg_quality", 80, &cfg->jpeg_quality) != BASE_OK)
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

BaseStatus media_cfg_from_ini(const BaseIni *ini, MediaConfig *out)
{
    MediaConfig cfg;
    if (ini == NULL || out == NULL)
    {
        return BASE_ERR;
    }
    memset(&cfg, 0, sizeof(cfg));
    if (Read(ini, &cfg) != BASE_OK || !Valid(&cfg))
    {
        return BASE_ERR;
    }
    *out = cfg;
    return BASE_OK;
}
