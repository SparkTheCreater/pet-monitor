/**
 * @file    algo_cfg.h
 * @brief   从 [algo] 填打开参数、模型路径和阈值。
 *
 * 模型路径按 ini 原文拷贝。相对路径由调用方接到 [base] dir 上。
 * score_min、nms_iou 先放在这里，检测随后再读。
 * 写错时返回 BASE_ERR，*out 不动。
 */
#ifndef ALGO_CFG_H
#define ALGO_CFG_H

#include "algo.h"
#include "ini.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define ALGO_PATH_MAX 256

typedef struct AlgoConfig
{
    AlgoOpenParam open;
    char detect_model[ALGO_PATH_MAX];
    char track_model[ALGO_PATH_MAX];
    char cat_model[ALGO_PATH_MAX];
    float score_min;
    float nms_iou;
}
AlgoConfig;

BaseStatus algo_cfg_from_ini(const BaseIni *ini, AlgoConfig *out);

#ifdef __cplusplus
}
#endif

#endif
