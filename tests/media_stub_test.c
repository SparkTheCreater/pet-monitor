#define _POSIX_C_SOURCE 200809L

/**
 * @file    media_stub_test.c
 * @brief   没有芯片时，解码打不开，画线和编码也不假装成功。
 */
#include "media_dec.h"
#include "media_enc.h"
#include "media_jpeg.h"
#include "media_mem.h"
#include "media_ai.h"
#include "media_npu.h"
#include "media_pic.h"
#include "media_sys.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_failed = 0;

static pthread_mutex_t g_cb_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cb_cv = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_gate_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_gate_cv = PTHREAD_COND_INITIALIZER;
static int g_cb_n;
static int g_go;
static int g_held;
static int g_inflight;
static int g_max_inflight;

typedef struct
{
    int kind;
    int cb_type;
    int status;
    int value;
    int param_len;
    int buf_size;
    char param[8];
    char tag[8];
    char buf[8];
}
CbItem;

static CbItem g_cb[8];

static void Expect(int cond, const char *name)
{
    if (cond)
    {
        return;
    }
    fprintf(stderr, "fail %s\n", name);
    g_failed = 1;
}

static void ResetCb(void)
{
    pthread_mutex_lock(&g_cb_mu);
    g_cb_n = 0;
    memset(g_cb, 0, sizeof(g_cb));
    pthread_mutex_unlock(&g_cb_mu);
}

static int WaitCb(int n)
{
    struct timespec ts;
    int ok;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;
    ok = 1;
    pthread_mutex_lock(&g_cb_mu);
    while (g_cb_n < n)
    {
        if (pthread_cond_timedwait(&g_cb_cv, &g_cb_mu, &ts) != 0)
        {
            ok = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_cb_mu);
    return ok;
}

static void OnCb(const MediaAiCallbackInfo *info)
{
    CbItem *item;
    pthread_mutex_lock(&g_cb_mu);
    if (g_cb_n < 8)
    {
        item = &g_cb[g_cb_n];
        memset(item, 0, sizeof(*item));
        item->kind = info->kind;
        item->cb_type = info->cb_type;
        item->status = info->status;
        item->param_len = info->param_len;
        item->buf_size = info->buf_size;
        if (info->param != NULL && info->param_len > 0 && info->param_len < (int)sizeof(item->param))
        {
            memcpy(item->param, info->param, (size_t)info->param_len);
        }
        if (info->tag != NULL)
        {
            snprintf(item->tag, sizeof(item->tag), "%s", (const char *)info->tag);
        }
        if (info->buf != NULL && info->buf_size > 0 && info->buf_size < (int)sizeof(item->buf))
        {
            memcpy(item->buf, info->buf, (size_t)info->buf_size);
        }
        if (info->buf != NULL && info->buf_size >= (int)sizeof(int))
        {
            item->value = *(const int *)info->buf;
        }
        g_cb_n += 1;
    }
    pthread_cond_signal(&g_cb_cv);
    pthread_mutex_unlock(&g_cb_mu);
}

static void ReleaseHold(void)
{
    pthread_mutex_lock(&g_gate_mu);
    g_go = 1;
    pthread_cond_broadcast(&g_gate_cv);
    pthread_mutex_unlock(&g_gate_mu);
}

static int WaitHeld(int n)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;
    pthread_mutex_lock(&g_gate_mu);
    while (g_held < n)
    {
        if (pthread_cond_timedwait(&g_gate_cv, &g_gate_mu, &ts) != 0)
        {
            pthread_mutex_unlock(&g_gate_mu);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_gate_mu);
    return 1;
}

static BaseStatus OkOpen(const char *model_path, const MediaAiOpenParam *param, void **impl)
{
    (void)model_path;
    (void)param;
    if (impl == NULL)
    {
        return BASE_ERR;
    }
    *impl = calloc(1, sizeof(int));
    if (*impl == NULL)
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

static void OkClose(void *impl)
{
    free(impl);
}

static BaseStatus OkInfer(void *impl)
{
    (void)impl;
    return BASE_OK;
}

static BaseStatus OkPost(void *impl, void *buf, int buf_size)
{
    (void)impl;
    if (buf == NULL || buf_size < (int)sizeof(int))
    {
        return BASE_ERR;
    }
    *(int *)buf = 7;
    return BASE_OK;
}

static BaseStatus HoldPre(void *impl, const BaseImage *frame)
{
    (void)impl;
    if (!base_image_valid(frame))
    {
        return BASE_ERR;
    }
    pthread_mutex_lock(&g_gate_mu);
    g_held += 1;
    pthread_cond_broadcast(&g_gate_cv);
    while (!g_go)
    {
        pthread_cond_wait(&g_gate_cv, &g_gate_mu);
    }
    pthread_mutex_unlock(&g_gate_mu);
    return BASE_OK;
}

static BaseStatus PassPre(void *impl, const BaseImage *frame)
{
    (void)impl;
    if (!base_image_valid(frame))
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

static BaseStatus OverlapPre(void *impl, const BaseImage *frame)
{
    struct timespec ts;
    (void)impl;
    if (!base_image_valid(frame))
    {
        return BASE_ERR;
    }
    pthread_mutex_lock(&g_gate_mu);
    g_inflight += 1;
    if (g_inflight > g_max_inflight)
    {
        g_max_inflight = g_inflight;
    }
    pthread_cond_broadcast(&g_gate_cv);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    while (g_inflight < 2)
    {
        if (pthread_cond_timedwait(&g_gate_cv, &g_gate_mu, &ts) != 0)
        {
            break;
        }
    }
    g_inflight -= 1;
    pthread_mutex_unlock(&g_gate_mu);
    return BASE_OK;
}

static int RegisterKind(int kind, const char *name, BaseStatus (*pre)(void *, const BaseImage *))
{
    static MediaAiModule mod[8];
    static int n;
    if (n >= 8)
    {
        return 0;
    }
    memset(&mod[n], 0, sizeof(mod[n]));
    mod[n].kind = kind;
    mod[n].name = name;
    mod[n].result_size = (int)sizeof(int);
    mod[n].open = OkOpen;
    mod[n].close = OkClose;
    mod[n].preprocess = pre;
    mod[n].infer = OkInfer;
    mod[n].postprocess = OkPost;
    if (media_ai_register(&mod[n]) != BASE_OK)
    {
        return 0;
    }
    n += 1;
    return 1;
}

static void FakeImage(BaseImage *image)
{
    memset(image, 0, sizeof(*image));
    image->handle = (void *)1;
    image->width = 32;
    image->height = 32;
    image->fd = -1;
}

static BaseStatus SendFrame(MediaAi *ai, const BaseImage *image, int priority, int exec, const char *tag)
{
    MediaAiInput in;
    memset(&in, 0, sizeof(in));
    in.type = MEDIA_AI_DATA_FRAME;
    in.frame = image;
    in.priority = priority;
    in.exec = exec;
    in.tag = tag;
    return media_ai_input(ai, &in);
}

static MediaAi *OpenDepth(int depth)
{
    MediaAiOpenParam param;
    MediaAi *ai;
    memset(&param, 0, sizeof(param));
    param.queue_depth = depth;
    ai = NULL;
    if (media_ai_open(&param, &ai) != BASE_OK)
    {
        return NULL;
    }
    return ai;
}

static void RunAi(void)
{
    MediaAi *ai;
    MediaAiOpenParam param;
    MediaAiOpenParam got;
    MediaAiInput in;
    BaseImage image;
    char cfg[4];
    int i;
    int seen_a;
    int seen_b;
    const char *header = "hi";
    const char *priv = "pv";
    const char *stream = "st";
    const char *audio = "au";

    ai = (MediaAi *)1;
    Expect(media_ai_open(NULL, NULL) == BASE_ERR, "ai open null");
    Expect(media_ai_load(NULL) == BASE_ERR, "ai load null");
    Expect(media_ai_load("no-such-ai-module.so") == BASE_ERR, "ai load missing");
    Expect(media_ai_unload(NULL) == BASE_ERR, "ai unload null");
    Expect(media_ai_add(NULL, MEDIA_AI_DETECT, "model.rknn") == BASE_ERR, "ai add null");
    Expect(media_ai_set_callback(NULL, MEDIA_AI_DETECT, NULL, NULL) == BASE_ERR, "ai callback null");
    Expect(media_ai_set_config(NULL, MEDIA_AI_DETECT, NULL, 0) == BASE_ERR, "ai config null");
    Expect(media_ai_get_config(NULL, MEDIA_AI_DETECT, cfg, 4) == BASE_ERR, "ai get config null");
    Expect(media_ai_input(NULL, NULL) == BASE_ERR, "ai input null");
    media_ai_close(NULL);

    memset(&param, 0, sizeof(param));
    param.width = -1;
    Expect(media_ai_open(&param, &ai) == BASE_ERR && ai == NULL, "ai width");
    param.width = 0;
    param.queue_depth = 9;
    ai = (MediaAi *)1;
    Expect(media_ai_open(&param, &ai) == BASE_ERR && ai == NULL, "ai depth");

    memset(&param, 0, sizeof(param));
    param.width = 1920;
    param.height = 1080;
    param.connect = 3;
    param.dev_id = 1;
    param.module_id = 7;
    ai = NULL;
    Expect(media_ai_open(&param, &ai) == BASE_OK && ai != NULL, "ai open");
    memset(&got, 0, sizeof(got));
    Expect(media_ai_get_param(ai, &got) == BASE_OK, "ai get param");
    Expect(got.width == 1920 && got.height == 1080 && got.connect == 3, "ai param size");
    Expect(got.dev_id == 1 && got.module_id == 7 && got.queue_depth == MEDIA_AI_QUEUE_DEFAULT, "ai param dev");
    Expect(media_ai_add(ai, MEDIA_AI_DETECT, "model.rknn") == BASE_ERR, "ai detect model");
    Expect(media_ai_add(ai, 999, "model.rknn") == BASE_ERR, "ai unknown");
    media_ai_close(ai);

    Expect(RegisterKind(100, "hold", HoldPre), "reg hold");
    Expect(RegisterKind(101, "serial-a", PassPre), "reg a");
    Expect(RegisterKind(102, "serial-b", PassPre), "reg b");
    Expect(RegisterKind(103, "parallel-a", OverlapPre), "reg pa");
    Expect(RegisterKind(104, "parallel-b", OverlapPre), "reg pb");

    ai = OpenDepth(2);
    Expect(ai != NULL, "ai slots open");
    Expect(media_ai_add(ai, 100, "m") == BASE_OK, "slot 100");
    Expect(media_ai_add(ai, 100, "m") == BASE_ERR, "slot dup");
    Expect(media_ai_add(ai, 101, "m") == BASE_OK, "slot 101");
    Expect(media_ai_add(ai, 102, "m") == BASE_OK, "slot 102");
    Expect(media_ai_add(ai, 103, "m") == BASE_OK, "slot 103");
    Expect(media_ai_add(ai, 104, "m") == BASE_ERR, "slot full");
    media_ai_close(ai);

    FakeImage(&image);
    pthread_mutex_lock(&g_gate_mu);
    g_go = 0;
    g_held = 0;
    pthread_mutex_unlock(&g_gate_mu);
    ResetCb();
    ai = OpenDepth(2);
    Expect(ai != NULL, "ai queue open");
    Expect(media_ai_add(ai, 100, "m") == BASE_OK, "queue add");
    memset(&in, 0, sizeof(in));
    in.type = MEDIA_AI_DATA_FRAME;
    in.frame = &image;
    Expect(media_ai_input(ai, &in) == BASE_ERR, "queue no callback");
    Expect(media_ai_set_callback(ai, 100, OnCb, NULL) == BASE_OK, "queue cb");
    Expect(media_ai_set_config(ai, 100, "pet", 3) == BASE_OK, "queue set cfg");
    memset(cfg, 0, sizeof(cfg));
    Expect(media_ai_get_config(ai, 100, cfg, 2) == BASE_ERR, "queue cfg short");
    Expect(media_ai_get_config(ai, 100, cfg, 3) == BASE_OK && memcmp(cfg, "pet", 3) == 0, "queue cfg");
    Expect(SendFrame(ai, &image, 0, MEDIA_AI_EXEC_SERIAL, "a") == BASE_OK, "queue a");
    Expect(WaitHeld(1), "queue held");
    Expect(SendFrame(ai, &image, 1, MEDIA_AI_EXEC_SERIAL, "low") == BASE_OK, "queue low");
    Expect(SendFrame(ai, &image, 5, MEDIA_AI_EXEC_SERIAL, "high") == BASE_OK, "queue high");
    Expect(SendFrame(ai, &image, 9, MEDIA_AI_EXEC_SERIAL, "drop") == BASE_AGAIN, "queue full");
    ReleaseHold();
    Expect(WaitCb(3), "queue done");
    Expect(strcmp(g_cb[0].tag, "a") == 0 && strcmp(g_cb[1].tag, "high") == 0 && strcmp(g_cb[2].tag, "low") == 0, "queue order");
    Expect(g_cb[0].cb_type == MEDIA_AI_CB_DATA && g_cb[0].value == 7 && g_cb[0].param_len == 3, "queue result");
    Expect(memcmp(g_cb[0].param, "pet", 3) == 0, "queue param");

    memset(&in, 0, sizeof(in));
    in.size = 2;
    in.type = MEDIA_AI_DATA_HEADER;
    in.data = header;
    in.tag = header;
    ResetCb();
    Expect(media_ai_input(ai, &in) == BASE_OK && WaitCb(1), "header");
    Expect(g_cb[0].cb_type == MEDIA_AI_CB_STATUS && g_cb[0].status == BASE_OK && memcmp(g_cb[0].buf, "hi", 2) == 0, "header cb");
    in.type = MEDIA_AI_DATA_PRIVATE;
    in.data = priv;
    in.tag = priv;
    ResetCb();
    Expect(media_ai_input(ai, &in) == BASE_OK && WaitCb(1), "private");
    Expect(memcmp(g_cb[0].buf, "pv", 2) == 0, "private buf");
    in.type = MEDIA_AI_DATA_STREAM;
    in.data = stream;
    in.tag = stream;
    ResetCb();
    Expect(media_ai_input(ai, &in) == BASE_OK && WaitCb(1), "stream");
    Expect(memcmp(g_cb[0].buf, "st", 2) == 0, "stream buf");
    in.type = MEDIA_AI_DATA_AUDIO;
    in.data = audio;
    in.tag = audio;
    ResetCb();
    Expect(media_ai_input(ai, &in) == BASE_OK && WaitCb(1), "audio");
    Expect(memcmp(g_cb[0].buf, "au", 2) == 0, "audio buf");
    media_ai_close(ai);

    ResetCb();
    ai = OpenDepth(2);
    Expect(ai != NULL, "serial open");
    Expect(media_ai_add(ai, 101, "m") == BASE_OK && media_ai_add(ai, 102, "m") == BASE_OK, "serial add");
    Expect(media_ai_set_callback(ai, 101, OnCb, NULL) == BASE_OK, "serial cb a");
    Expect(media_ai_set_callback(ai, 102, OnCb, NULL) == BASE_OK, "serial cb b");
    Expect(SendFrame(ai, &image, 0, MEDIA_AI_EXEC_SERIAL, "s") == BASE_OK, "serial send");
    Expect(WaitCb(2), "serial done");
    Expect(g_cb[0].kind == 101 && g_cb[1].kind == 102, "serial order");
    Expect(g_cb[0].cb_type == MEDIA_AI_CB_DATA && g_cb[1].cb_type == MEDIA_AI_CB_DATA, "serial data");
    media_ai_close(ai);

    ResetCb();
    pthread_mutex_lock(&g_gate_mu);
    g_inflight = 0;
    g_max_inflight = 0;
    pthread_mutex_unlock(&g_gate_mu);
    ai = OpenDepth(2);
    Expect(ai != NULL, "parallel open");
    Expect(media_ai_add(ai, 103, "m") == BASE_OK && media_ai_add(ai, 104, "m") == BASE_OK, "parallel add");
    Expect(media_ai_set_callback(ai, 103, OnCb, NULL) == BASE_OK, "parallel cb a");
    Expect(media_ai_set_callback(ai, 104, OnCb, NULL) == BASE_OK, "parallel cb b");
    Expect(SendFrame(ai, &image, 0, MEDIA_AI_EXEC_PARALLEL, "p") == BASE_OK, "parallel send");
    Expect(WaitCb(2), "parallel done");
    seen_a = 0;
    seen_b = 0;
    for (i = 0; i < 2; ++i)
    {
        if (g_cb[i].kind == 103 && g_cb[i].cb_type == MEDIA_AI_CB_DATA)
        {
            seen_a = 1;
        }
        if (g_cb[i].kind == 104 && g_cb[i].cb_type == MEDIA_AI_CB_DATA)
        {
            seen_b = 1;
        }
    }
    Expect(seen_a && seen_b && g_max_inflight == 2, "parallel overlap");
    media_ai_close(ai);

    Expect(media_ai_load(MEDIA_AI_PLUGIN) == BASE_OK, "plugin load");
    Expect(media_ai_load(MEDIA_AI_PLUGIN) == BASE_ERR, "plugin twice");
    ResetCb();
    ai = OpenDepth(1);
    Expect(ai != NULL, "plugin open");
    Expect(media_ai_add(ai, 80, "m") == BASE_OK, "plugin add");
    Expect(media_ai_set_callback(ai, 80, OnCb, NULL) == BASE_OK, "plugin cb");
    Expect(media_ai_unload(MEDIA_AI_PLUGIN) == BASE_ERR, "plugin busy");
    Expect(SendFrame(ai, &image, 0, MEDIA_AI_EXEC_SERIAL, "plugin") == BASE_OK, "plugin send");
    Expect(WaitCb(1), "plugin done");
    Expect(g_cb[0].kind == 80 && g_cb[0].cb_type == MEDIA_AI_CB_DATA && g_cb[0].value == 42, "plugin result");
    media_ai_close(ai);
    Expect(media_ai_unload(MEDIA_AI_PLUGIN) == BASE_OK, "plugin unload");
    Expect(media_ai_unload(MEDIA_AI_PLUGIN) == BASE_ERR, "plugin unloaded");
}

int main(void)
{
    MediaDec *dec = (MediaDec *)1;
    MediaEnc *enc = (MediaEnc *)1;
    MediaEncParam param;
    MediaNpu *npu = (MediaNpu *)1;
    MediaNpuTensor tensor;
    BaseStatus status;
    BaseImage image;
    int count;

    Expect(media_init() == BASE_ERR, "media init");
    media_deinit();

    memset(&image, 0, sizeof(image));
    image.handle = (void *)1;
    image.fd = 3;
    Expect(media_mem_alloc(16, 16, BASE_PIX_NV12, &image) == BASE_ERR, "mem alloc");
    Expect(image.handle == NULL && image.fd == -1, "mem alloc cleared");
    media_mem_free(&image);
    Expect(media_mem_flush(&image) == BASE_ERR, "mem flush");

    Expect(media_npu_open("model.rknn", &npu) == BASE_ERR, "npu open");
    Expect(npu == NULL, "npu cleared");
    media_npu_close(NULL);
    Expect(media_npu_set_core(NULL, 0) == BASE_ERR, "npu core");
    Expect(media_npu_input_info(NULL, &tensor) == BASE_ERR, "npu input");
    count = 1;
    Expect(media_npu_output_count(NULL, &count) == BASE_ERR, "npu count");
    Expect(count == 0, "npu count cleared");
    Expect(media_npu_output_info(NULL, 0, &tensor) == BASE_ERR, "npu output");
    Expect(media_npu_run(NULL, NULL, NULL, NULL) == BASE_ERR, "npu run");

    RunAi();

    Expect(media_dec_open(BASE_CODEC_H264, 4, NULL, 0, &dec) == BASE_ERR, "decoder open");
    Expect(dec == NULL, "decoder cleared");
    media_dec_close(NULL);
    Expect(media_dec_send(NULL, NULL, 0, 0, 0) == BASE_ERR, "decoder send");

    uint8_t *jpeg = (uint8_t *)1;
    size_t jpeg_len = 1;
    Expect(media_jpeg_encode(NULL, 80, &jpeg, &jpeg_len) == BASE_ERR, "jpeg encode");
    Expect(jpeg == NULL && jpeg_len == 0, "jpeg encode cleared");
    media_jpeg_free(NULL);

    memset(&image, 0, sizeof(image));
    image.handle = (void *)1;
    image.fd = 3;
    Expect(media_jpeg_decode(NULL, 0, &image) == BASE_ERR, "jpeg decode");
    Expect(image.handle == NULL && image.fd == -1, "jpeg decode cleared");
    Expect(media_jpeg_decode(NULL, 0, NULL) == BASE_ERR, "jpeg decode null");

    Expect(media_pic_scale(NULL, NULL, NULL, NULL) == BASE_ERR, "scale");
    Expect(media_pic_preprocess(NULL, NULL, NULL) == BASE_ERR, "preprocess");
    Expect(media_pic_line(NULL, 0, 0, 10, 2, 1, 0) == BASE_ERR, "line");
    Expect(media_pic_box(NULL, NULL, 0, 0, 2) == BASE_ERR, "box");
    Expect(media_pic_fill(NULL, NULL, 0) == BASE_ERR, "fill");
    Expect(media_pic_rotate(NULL, NULL, 90) == BASE_ERR, "rotate");
    Expect(media_pic_flip(NULL, NULL, 0) == BASE_ERR, "flip");
    Expect(media_pic_translate(NULL, NULL, 0, 0) == BASE_ERR, "translate");
    Expect(media_enc_set_roi(NULL, NULL, 0) == BASE_ERR, "roi");
    Expect(media_enc_set_osd(NULL, NULL, 0) == BASE_ERR, "osd");
    Expect(media_enc_set_osd_palette(NULL, NULL, 0) == BASE_ERR, "osd palette");

    memset(&param, 0, sizeof(param));
    Expect(media_enc_open(NULL, &enc) == BASE_ERR, "encoder null");
    Expect(enc == NULL, "encoder cleared");

    param.codec = BASE_CODEC_H264;
    param.format = BASE_PIX_NV12;
    param.width = 640;
    param.height = 480;
    param.fps = 25;
    param.gop = 50;
    param.rc_mode = MEDIA_RC_CBR;
    param.bitrate = 2000000;
    param.qp_init = 26;
    param.qp_min = 10;
    param.qp_max = 51;
    param.profile = 0;
    param.level = 40;
    Expect(media_enc_open(&param, &enc) == BASE_ERR, "encoder profile");
    Expect(enc == NULL, "encoder profile cleared");
    param.profile = 100;
    status = media_enc_open(&param, &enc);
    Expect(status == BASE_UNSUPPORTED, "encoder unsupported");
    Expect(enc == NULL, "encoder still empty");
    media_enc_close(NULL);
    Expect(media_enc_send(NULL, NULL, 0) == BASE_ERR, "encoder send");
    Expect(media_enc_receive(NULL, NULL) == BASE_ERR, "encoder receive");
    media_enc_packet_free(NULL);
    Expect(media_enc_request_keyframe(NULL) == BASE_ERR, "encoder key");
    const uint8_t *extra = (const uint8_t *)1;
    size_t extra_size = 1;
    Expect(media_enc_extradata(NULL, &extra, &extra_size) == BASE_ERR, "encoder extradata");
    Expect(extra == NULL && extra_size == 0, "encoder extradata cleared");
    Expect(media_enc_set_bitrate(NULL, 1000000) == BASE_ERR, "encoder bitrate");
    Expect(media_enc_set_rotation(NULL, 90) == BASE_ERR, "encoder rotation");
    Expect(media_enc_set_mirror(NULL, 1) == BASE_ERR, "encoder mirror");
    return g_failed ? 1 : 0;
}
