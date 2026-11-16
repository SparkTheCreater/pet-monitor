/**
 * @file    app_judge.h
 * @brief   一拍的事实和要不要做下一步。判断不画框，也不发信。
 *
 * keep、send、drop 各自为真才做对应的那一步。为假就跳过。
 * 真正去做的是另一个函数，见 app_act_run。
 */
#ifndef APP_JUDGE_H
#define APP_JUDGE_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct AppCamera;
struct AlgoDetectResult;

typedef struct AppFact
{
    int64_t now_ms;
    int has_cat;
    const struct AppCamera *camera;
    const struct AlgoDetectResult *result;
}
AppFact;

typedef struct AppVerdict
{
    /* 这一帧要画框并编成 JPEG 留下来。 */
    int keep;
    /* 留下的 JPEG 交出去。 */
    int send;
    /* 留下的 JPEG 作废。 */
    int drop;
    /* 这次由两只猫规则触发。文件名用它。 */
    int pair;
}
AppVerdict;

typedef void (*AppJudgeFn)(const AppFact *fact, void *user, AppVerdict *out);

/* JPEG 真正留下之后才调用。编码失败不要调用。 */
typedef void (*AppJudgeKeptFn)(void *user);

#ifdef __cplusplus
}
#endif

#endif
