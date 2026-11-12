/**
 * @file    media_ai_internal.h
 * @brief   内置算法的三步接口。插件不走这里。
 */
#ifndef MEDIA_AI_INTERNAL_H
#define MEDIA_AI_INTERNAL_H

#include "media_ai.h"

/* 内置算法登记。不再走 media_ai_register，避免加载过程里递归。 */
BaseStatus media_ai_register_mod(const MediaAiModule *mod);

typedef struct AiDetect AiDetect;

BaseStatus ai_detect_open(const char *model_path, const MediaAiOpenParam *param, AiDetect **out);
void ai_detect_close(AiDetect *det);
BaseStatus ai_detect_preprocess(AiDetect *det, const BaseImage *frame);
BaseStatus ai_detect_infer(AiDetect *det);
BaseStatus ai_detect_postprocess(AiDetect *det, MediaAiBox *boxes, int capacity, int *count);
void ai_detect_register(void);

typedef struct AiTrack AiTrack;

BaseStatus ai_track_open(const char *model_path, const MediaAiOpenParam *param, AiTrack **out);
void ai_track_close(AiTrack *track);
BaseStatus ai_track_preprocess(AiTrack *track, const BaseImage *frame);
BaseStatus ai_track_infer(AiTrack *track);
BaseStatus ai_track_postprocess(AiTrack *track, MediaAiTrackResult *result);
void ai_track_register(void);

typedef struct AiRecog AiRecog;

BaseStatus ai_recog_open(const char *model_path, const MediaAiOpenParam *param, AiRecog **out);
void ai_recog_close(AiRecog *recog);
BaseStatus ai_recog_preprocess(AiRecog *recog, const BaseImage *frame);
BaseStatus ai_recog_infer(AiRecog *recog);
BaseStatus ai_recog_postprocess(AiRecog *recog, MediaAiRecog *out);
void ai_recog_register(void);

#endif
