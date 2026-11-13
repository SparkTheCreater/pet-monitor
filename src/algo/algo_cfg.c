/**
 * @file    algo_cfg.c
 * @brief   只读 [algo]。不打开模型。
 */
#include "algo_cfg.h"

#include <string.h>

static const char kYolo[] = "models/yolov5s-640-640.rknn";
static const char kCat[] = "models/Catto.rknn";

static BaseStatus Model(const BaseIni *ini, const char *key, const char *def, char *dst, size_t n)
{
    if (base_ini_copy(ini, "algo", key, def, dst, n) != BASE_OK || dst[0] == '\0')
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

static BaseStatus Read(const BaseIni *ini, AlgoConfig *cfg)
{
    double score;
    double nms;
    if (Model(ini, "detect_model", kYolo, cfg->detect_model, sizeof(cfg->detect_model)) != BASE_OK ||
        Model(ini, "track_model", kYolo, cfg->track_model, sizeof(cfg->track_model)) != BASE_OK ||
        Model(ini, "cat_model", kCat, cfg->cat_model, sizeof(cfg->cat_model)) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (base_ini_real(ini, "algo", "score_min", 0.25, &score) != BASE_OK ||
        base_ini_real(ini, "algo", "nms_iou", 0.45, &nms) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (score < 0.0 || score > 1.0 || nms < 0.0 || nms > 1.0)
    {
        return BASE_ERR;
    }
    cfg->score_min = (float)score;
    cfg->nms_iou = (float)nms;
    if (base_ini_int(ini, "algo", "width", 0, &cfg->open.width) != BASE_OK ||
        base_ini_int(ini, "algo", "height", 0, &cfg->open.height) != BASE_OK ||
        base_ini_int(ini, "algo", "connect", 0, &cfg->open.connect) != BASE_OK ||
        base_ini_int(ini, "algo", "dev_id", 0, &cfg->open.dev_id) != BASE_OK ||
        base_ini_int(ini, "algo", "module_id", 0, &cfg->open.module_id) != BASE_OK ||
        base_ini_int(ini, "algo", "queue_depth", ALGO_QUEUE_DEFAULT, &cfg->open.queue_depth) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (cfg->open.width < 0 || cfg->open.height < 0 || cfg->open.dev_id < 0)
    {
        return BASE_ERR;
    }
    if (cfg->open.queue_depth < 0 || cfg->open.queue_depth > ALGO_QUEUE_MAX)
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

BaseStatus algo_cfg_from_ini(const BaseIni *ini, AlgoConfig *out)
{
    AlgoConfig cfg;
    if (ini == NULL || out == NULL)
    {
        return BASE_ERR;
    }
    memset(&cfg, 0, sizeof(cfg));
    if (Read(ini, &cfg) != BASE_OK)
    {
        return BASE_ERR;
    }
    *out = cfg;
    return BASE_OK;
}

const char *algo_cfg_model(const AlgoConfig *cfg, int kind)
{
    if (cfg == NULL)
    {
        return NULL;
    }
    if (kind == ALGO_DETECT)
    {
        return cfg->detect_model;
    }
    if (kind == ALGO_TRACK)
    {
        return cfg->track_model;
    }
    if (kind == ALGO_CAT_DET)
    {
        return cfg->cat_model;
    }
    return NULL;
}
