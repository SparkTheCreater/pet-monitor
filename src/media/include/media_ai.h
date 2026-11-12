/**
 * @file    media_ai.h
 * @brief   一个句柄挂多种算法。送入的数据进队列，工作线程按固定顺序跑完再回调。
 */
#ifndef MEDIA_AI_H
#define MEDIA_AI_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct MediaAi MediaAi;

/* 内置算法从 0 起。插件可以登记别的正整数。MEDIA_AI_CAT_DET 留给猫检测插件。 */
typedef enum
{
    MEDIA_AI_DETECT = 0,
    MEDIA_AI_TRACK,
    MEDIA_AI_RECOGNIZE,
    MEDIA_AI_CAT_DET
}
MediaAiKind;

/* 检测和追踪对外的类别。后处理只留下猫。 */
typedef enum
{
    MEDIA_AI_CAT = 0
}
MediaAiClass;

/* 回调里 buf 的含义。推理结果用 DATA，失败和旁路数据用 STATUS。 */
typedef enum
{
    MEDIA_AI_CB_DATA = 1,
    MEDIA_AI_CB_STATUS = 2
}
MediaAiCbType;

/* 送进 media_ai_input 的数据种类。 */
typedef enum
{
    MEDIA_AI_DATA_FRAME = 0,
    MEDIA_AI_DATA_HEADER,
    MEDIA_AI_DATA_PRIVATE,
    MEDIA_AI_DATA_STREAM,
    MEDIA_AI_DATA_AUDIO
}
MediaAiDataType;

/* 同一帧上几种算法怎么跑。串行按挂上的顺序，并行每种各跑各的模型。 */
typedef enum
{
    MEDIA_AI_EXEC_SERIAL = 0,
    MEDIA_AI_EXEC_PARALLEL
}
MediaAiExec;

/* 一次结果最多留下的目标数。一个句柄最多同时挂这么多种算法。 */
#define MEDIA_AI_MAX 32
#define MEDIA_AI_SLOT_MAX 4

/* 每种算法的配置原文最长这么多。0 表示创建时用默认队列深度。 */
#define MEDIA_AI_CFG_MAX 512
#define MEDIA_AI_QUEUE_DEFAULT 2
#define MEDIA_AI_QUEUE_MAX 8

/* 算法写结果的缓冲上限。插件的 result_size 不能超过它。 */
#define MEDIA_AI_RESULT_MAX 4096

/* 检测框。box 是原图像素，x、y 为左上角。 */
typedef struct MediaAiBox
{
    int type;
    float score;
    BaseRect box;
}
MediaAiBox;

/* 检测回调的 buf。count 为 0 表示这帧没有目标。 */
typedef struct MediaAiDetectResult
{
    int count;
    MediaAiBox box[MEDIA_AI_MAX];
}
MediaAiDetectResult;

/* 追踪框。id 在同一路算法的连续帧里保持。 */
typedef struct MediaAiTrack
{
    int id;
    int type;
    float score;
    BaseRect box;
}
MediaAiTrack;

/* 追踪回调的 buf。 */
typedef struct MediaAiTrackResult
{
    int count;
    MediaAiTrack item[MEDIA_AI_MAX];
}
MediaAiTrackResult;

/* 识别回调的 buf。cls 是模型输出的类别序号。 */
typedef struct MediaAiRecog
{
    int cls;
    float score;
}
MediaAiRecog;

/*
 * 创建句柄时记下的参数。
 * width、height 为 0 表示不指定分辨率。connect 是连接方式，由调用方自己解释。
 * dev_id 大于 0 时，内置算法用它选择 NPU 核。module_id 只保存，供插件打开时读取。
 * queue_depth 为 0 时用 MEDIA_AI_QUEUE_DEFAULT。满队列时送入返回 BASE_AGAIN。
 */
typedef struct MediaAiOpenParam
{
    int width;
    int height;
    int connect;
    int dev_id;
    int module_id;
    int queue_depth;
}
MediaAiOpenParam;

/*
 * 一次回调。kind 是算法类型。
 * DATA 时 buf 指向该算法自己的结果，长度是 buf_size。
 * STATUS 时 status 为 BASE_OK 或 BASE_ERR；旁路数据的原文在 buf。
 * param 是这种算法当前的配置。tag 是送入时带的标记。
 * user 是登记回调时传入的指针。这些指针只在回调返回前有效。
 */
typedef struct MediaAiCallbackInfo
{
    int kind;
    int cb_type;
    int status;
    const void *buf;
    int buf_size;
    const void *param;
    int param_len;
    const void *tag;
    void *user;
}
MediaAiCallbackInfo;

/* 一种算法一份。回调可能发生在工作线程或并行线程里，里面不能关闭句柄。 */
typedef void (*MediaAiCallback)(const MediaAiCallbackInfo *info);

/*
 * 一次送入。type 取 MediaAiDataType。
 * FRAME 要带有效的 frame。其余种类带 data 和 size，帧指针可空。
 * 队列在调用返回后才跑，frame、data、tag 要保持到这次送入的回调都返回。
 * priority 越大越先跑。exec 只对 FRAME 起作用，取 MediaAiExec。
 */
typedef struct MediaAiInput
{
    int type;
    const BaseImage *frame;
    const void *data;
    int size;
    int priority;
    int exec;
    const void *tag;
}
MediaAiInput;

/*
 * 一种算法的入口。插件导出 media_ai_module，返回它的指针。
 * 指针和里面的函数在卸载前一直有效。
 * result_size 是结果缓冲的字节数。side 处理帧以外的数据，没有就填空。
 */
typedef struct MediaAiModule
{
    int kind;
    const char *name;
    int result_size;
    BaseStatus (*open)(const char *model_path, const MediaAiOpenParam *param, void **impl);
    void (*close)(void *impl);
    BaseStatus (*preprocess)(void *impl, const BaseImage *frame);
    BaseStatus (*infer)(void *impl);
    BaseStatus (*postprocess)(void *impl, void *buf, int buf_size);
    BaseStatus (*side)(void *impl, const MediaAiInput *in);
}
MediaAiModule;

/* 同步。登记一种算法。同一种类重复登记失败。mod 要一直有效。 */
BaseStatus media_ai_register(const MediaAiModule *mod);

/*
 * 同步。加载插件。插件导出 const MediaAiModule *media_ai_module(void)。
 * 同一种类已经在表里时失败，并卸下这次打开的库。
 */
BaseStatus media_ai_load(const char *path);

/* 同步。按加载路径卸载。还有句柄挂着这种算法时失败。 */
BaseStatus media_ai_unload(const char *path);

/* 同步。param 可空，表示默认队列和零配置。失败时 *out 为空。 */
BaseStatus media_ai_open(const MediaAiOpenParam *param, MediaAi **out);

/* 同步。等队列里已收下的数据跑完再销毁。空指针直接返回。 */
void media_ai_close(MediaAi *ai);

/* 同步。读回创建参数，队列深度是实际采用的值。 */
BaseStatus media_ai_get_param(MediaAi *ai, MediaAiOpenParam *out);

/*
 * 同步。往句柄上挂一种算法，最多 MEDIA_AI_SLOT_MAX 种。
 * 队列不空或正在跑时失败。模型打不开时失败。
 */
BaseStatus media_ai_add(MediaAi *ai, int kind, const char *model_path);

/*
 * 同步。登记这种算法的结果回调。cb 为空表示取消。
 * 一种算法一份回调。没有回调的算法不会跑。
 */
BaseStatus media_ai_set_callback(MediaAi *ai, int kind, MediaAiCallback cb, void *user);

/* 同步。保存这种算法的配置原文。cfg 为空或 len 为 0 时清掉。 */
BaseStatus media_ai_set_config(MediaAi *ai, int kind, const void *cfg, int len);

/* 同步。拷出配置。len 要不少于已保存的长度。 */
BaseStatus media_ai_get_config(MediaAi *ai, int kind, void *cfg, int len);

/*
 * 异步。把这一包放进队列。
 * 队列满返回 BASE_AGAIN，调用方稍后用同一包再送。
 * 没有任何已登记回调时失败，不进队列。
 * 推理失败通过 STATUS 回调，不从这里返回。
 */
BaseStatus media_ai_input(MediaAi *ai, const MediaAiInput *in);

#ifdef __cplusplus
}
#endif

#endif
