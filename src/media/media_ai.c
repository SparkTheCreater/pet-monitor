/**
 * @file    media_ai.c
 * @brief   算法注册表、句柄上的多路算法，以及按队列送入后的回调。
 */
#define _POSIX_C_SOURCE 200809L

#include "media_ai.h"

#include "media_ai_internal.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MEDIA_AI_MOD_MAX 16

typedef struct
{
    int used;
    int kind;
    const MediaAiModule *mod;
    void *impl;
    MediaAiCallback cb;
    void *user;
    unsigned char cfg[MEDIA_AI_CFG_MAX];
    int cfg_len;
    unsigned char result[MEDIA_AI_RESULT_MAX];
}
Slot;

typedef struct
{
    uint64_t seq;
    int type;
    int priority;
    int exec;
    int has_frame;
    BaseImage frame;
    const void *data;
    int size;
    const void *tag;
}
Job;

typedef struct
{
    int kind;
    void *impl;
    const MediaAiModule *mod;
    MediaAiCallback cb;
    void *user;
    unsigned char cfg[MEDIA_AI_CFG_MAX];
    int cfg_len;
    unsigned char *result;
}
SlotView;

typedef struct
{
    int used;
    const MediaAiModule *mod;
    void *so;
    char *path;
    int refs;
}
ModRec;

struct MediaAi
{
    MediaAiOpenParam param;
    Slot slot[MEDIA_AI_SLOT_MAX];
    Job q[MEDIA_AI_QUEUE_MAX];
    int q_count;
    uint64_t seq;
    int stop;
    int running;
    int worker_on;
    pthread_t worker;
    pthread_mutex_t mu;
    pthread_cond_t cv;
};

static ModRec g_mod[MEDIA_AI_MOD_MAX];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static int ModuleOk(const MediaAiModule *mod)
{
    if (mod == NULL || mod->open == NULL || mod->close == NULL)
    {
        return 0;
    }
    if (mod->preprocess == NULL || mod->infer == NULL || mod->postprocess == NULL)
    {
        return 0;
    }
    return mod->result_size > 0 && mod->result_size <= MEDIA_AI_RESULT_MAX;
}

static BaseStatus Adopt(const MediaAiModule *mod, void *so, const char *path)
{
    int i;
    int slot;
    char *copy;
    if (!ModuleOk(mod))
    {
        return BASE_ERR;
    }
    copy = NULL;
    if (path != NULL)
    {
        copy = strdup(path);
        if (copy == NULL)
        {
            return BASE_ERR;
        }
    }
    pthread_mutex_lock(&g_mu);
    slot = -1;
    for (i = 0; i < MEDIA_AI_MOD_MAX; ++i)
    {
        if (g_mod[i].used && g_mod[i].mod->kind == mod->kind)
        {
            pthread_mutex_unlock(&g_mu);
            free(copy);
            return BASE_ERR;
        }
        if (slot < 0 && !g_mod[i].used)
        {
            slot = i;
        }
    }
    if (slot < 0)
    {
        pthread_mutex_unlock(&g_mu);
        free(copy);
        return BASE_ERR;
    }
    g_mod[slot].used = 1;
    g_mod[slot].mod = mod;
    g_mod[slot].so = so;
    g_mod[slot].path = copy;
    g_mod[slot].refs = 0;
    pthread_mutex_unlock(&g_mu);
    return BASE_OK;
}

static const MediaAiModule *Retain(int kind)
{
    int i;
    const MediaAiModule *mod;
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < MEDIA_AI_MOD_MAX; ++i)
    {
        if (g_mod[i].used && g_mod[i].mod->kind == kind)
        {
            g_mod[i].refs += 1;
            mod = g_mod[i].mod;
            pthread_mutex_unlock(&g_mu);
            return mod;
        }
    }
    pthread_mutex_unlock(&g_mu);
    return NULL;
}

static void ReleaseMod(const MediaAiModule *mod)
{
    int i;
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < MEDIA_AI_MOD_MAX; ++i)
    {
        if (g_mod[i].used && g_mod[i].mod == mod && g_mod[i].refs > 0)
        {
            g_mod[i].refs -= 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_mu);
}

static void *Worker(void *arg);

/* 静态库靠这三次调用把检测、追踪、识别链进来。插件走 media_ai_load。 */
static void LoadBuiltin(void)
{
    ai_detect_register();
    ai_track_register();
    ai_recog_register();
}

static void Ensure(void)
{
    pthread_once(&g_once, LoadBuiltin);
}

BaseStatus media_ai_register_mod(const MediaAiModule *mod)
{
    return Adopt(mod, NULL, NULL);
}

BaseStatus media_ai_register(const MediaAiModule *mod)
{
    Ensure();
    return media_ai_register_mod(mod);
}

BaseStatus media_ai_load(const char *path)
{
    void *so;
    const MediaAiModule *(*fn)(void);
    const MediaAiModule *mod;
    if (path == NULL || path[0] == '\0')
    {
        return BASE_ERR;
    }
    Ensure();
    so = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (so == NULL)
    {
        return BASE_ERR;
    }
    fn = (const MediaAiModule *(*)(void))dlsym(so, "media_ai_module");
    if (fn == NULL)
    {
        dlclose(so);
        return BASE_ERR;
    }
    mod = fn();
    if (Adopt(mod, so, path) != BASE_OK)
    {
        dlclose(so);
        return BASE_ERR;
    }
    return BASE_OK;
}

BaseStatus media_ai_unload(const char *path)
{
    int i;
    void *so;
    char *copy;
    if (path == NULL || path[0] == '\0')
    {
        return BASE_ERR;
    }
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < MEDIA_AI_MOD_MAX; ++i)
    {
        if (!g_mod[i].used || g_mod[i].path == NULL || strcmp(g_mod[i].path, path) != 0)
        {
            continue;
        }
        if (g_mod[i].refs > 0)
        {
            pthread_mutex_unlock(&g_mu);
            return BASE_ERR;
        }
        so = g_mod[i].so;
        copy = g_mod[i].path;
        memset(&g_mod[i], 0, sizeof(g_mod[i]));
        pthread_mutex_unlock(&g_mu);
        free(copy);
        if (so != NULL)
        {
            dlclose(so);
        }
        return BASE_OK;
    }
    pthread_mutex_unlock(&g_mu);
    return BASE_ERR;
}

static int Busy(const MediaAi *ai)
{
    return ai->stop || ai->running || ai->q_count > 0;
}

static int Opening(const MediaAi *ai)
{
    int i;
    for (i = 0; i < MEDIA_AI_SLOT_MAX; ++i)
    {
        if (ai->slot[i].used && ai->slot[i].impl == NULL)
        {
            return 1;
        }
    }
    return 0;
}

static int HasKind(const MediaAi *ai, int kind)
{
    int i;
    for (i = 0; i < MEDIA_AI_SLOT_MAX; ++i)
    {
        if (ai->slot[i].used && ai->slot[i].kind == kind)
        {
            return 1;
        }
    }
    return 0;
}

static Slot *FindSlot(MediaAi *ai, int kind)
{
    int i;
    for (i = 0; i < MEDIA_AI_SLOT_MAX; ++i)
    {
        if (ai->slot[i].used && ai->slot[i].impl != NULL && ai->slot[i].kind == kind)
        {
            return &ai->slot[i];
        }
    }
    return NULL;
}

BaseStatus media_ai_open(const MediaAiOpenParam *param, MediaAi **out)
{
    MediaAi *ai;
    int depth;
    if (out != NULL)
    {
        *out = NULL;
    }
    if (out == NULL)
    {
        return BASE_ERR;
    }
    depth = MEDIA_AI_QUEUE_DEFAULT;
    if (param != NULL)
    {
        if (param->width < 0 || param->height < 0 || param->queue_depth < 0)
        {
            return BASE_ERR;
        }
        if (param->queue_depth > MEDIA_AI_QUEUE_MAX)
        {
            return BASE_ERR;
        }
        if (param->queue_depth > 0)
        {
            depth = param->queue_depth;
        }
    }
    Ensure();
    ai = (MediaAi *)calloc(1, sizeof(*ai));
    if (ai == NULL)
    {
        return BASE_ERR;
    }
    if (param != NULL)
    {
        ai->param = *param;
    }
    ai->param.queue_depth = depth;
    if (pthread_mutex_init(&ai->mu, NULL) != 0)
    {
        free(ai);
        return BASE_ERR;
    }
    if (pthread_cond_init(&ai->cv, NULL) != 0)
    {
        pthread_mutex_destroy(&ai->mu);
        free(ai);
        return BASE_ERR;
    }
    if (pthread_create(&ai->worker, NULL, Worker, ai) != 0)
    {
        pthread_cond_destroy(&ai->cv);
        pthread_mutex_destroy(&ai->mu);
        free(ai);
        return BASE_ERR;
    }
    ai->worker_on = 1;
    *out = ai;
    return BASE_OK;
}

void media_ai_close(MediaAi *ai)
{
    int i;
    if (ai == NULL)
    {
        return;
    }
    pthread_mutex_lock(&ai->mu);
    ai->stop = 1;
    while (Opening(ai))
    {
        pthread_cond_wait(&ai->cv, &ai->mu);
    }
    pthread_cond_broadcast(&ai->cv);
    pthread_mutex_unlock(&ai->mu);
    if (ai->worker_on)
    {
        pthread_join(ai->worker, NULL);
    }
    for (i = 0; i < MEDIA_AI_SLOT_MAX; ++i)
    {
        if (ai->slot[i].used && ai->slot[i].impl != NULL && ai->slot[i].mod != NULL)
        {
            ai->slot[i].mod->close(ai->slot[i].impl);
            ReleaseMod(ai->slot[i].mod);
        }
    }
    pthread_cond_destroy(&ai->cv);
    pthread_mutex_destroy(&ai->mu);
    free(ai);
}

BaseStatus media_ai_get_param(MediaAi *ai, MediaAiOpenParam *out)
{
    if (ai == NULL || out == NULL)
    {
        return BASE_ERR;
    }
    pthread_mutex_lock(&ai->mu);
    *out = ai->param;
    pthread_mutex_unlock(&ai->mu);
    return BASE_OK;
}

BaseStatus media_ai_add(MediaAi *ai, int kind, const char *model_path)
{
    const MediaAiModule *mod;
    void *impl;
    int i;
    int slot;
    if (ai == NULL || model_path == NULL)
    {
        return BASE_ERR;
    }
    mod = Retain(kind);
    if (mod == NULL)
    {
        return BASE_ERR;
    }
    pthread_mutex_lock(&ai->mu);
    if (Busy(ai) || HasKind(ai, kind))
    {
        pthread_mutex_unlock(&ai->mu);
        ReleaseMod(mod);
        return BASE_ERR;
    }
    slot = -1;
    for (i = 0; i < MEDIA_AI_SLOT_MAX; ++i)
    {
        if (!ai->slot[i].used)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        pthread_mutex_unlock(&ai->mu);
        ReleaseMod(mod);
        return BASE_ERR;
    }
    ai->slot[slot].used = 1;
    ai->slot[slot].kind = kind;
    ai->slot[slot].mod = mod;
    pthread_mutex_unlock(&ai->mu);
    impl = NULL;
    if (mod->open(model_path, &ai->param, &impl) != BASE_OK)
    {
        pthread_mutex_lock(&ai->mu);
        memset(&ai->slot[slot], 0, sizeof(ai->slot[slot]));
        pthread_cond_broadcast(&ai->cv);
        pthread_mutex_unlock(&ai->mu);
        ReleaseMod(mod);
        return BASE_ERR;
    }
    pthread_mutex_lock(&ai->mu);
    if (ai->stop)
    {
        memset(&ai->slot[slot], 0, sizeof(ai->slot[slot]));
        pthread_cond_broadcast(&ai->cv);
        pthread_mutex_unlock(&ai->mu);
        mod->close(impl);
        ReleaseMod(mod);
        return BASE_ERR;
    }
    ai->slot[slot].impl = impl;
    pthread_cond_broadcast(&ai->cv);
    pthread_mutex_unlock(&ai->mu);
    return BASE_OK;
}

BaseStatus media_ai_set_callback(MediaAi *ai, int kind, MediaAiCallback cb, void *user)
{
    Slot *slot;
    if (ai == NULL)
    {
        return BASE_ERR;
    }
    pthread_mutex_lock(&ai->mu);
    slot = FindSlot(ai, kind);
    if (slot == NULL || ai->stop)
    {
        pthread_mutex_unlock(&ai->mu);
        return BASE_ERR;
    }
    slot->cb = cb;
    slot->user = user;
    pthread_mutex_unlock(&ai->mu);
    return BASE_OK;
}

BaseStatus media_ai_set_config(MediaAi *ai, int kind, const void *cfg, int len)
{
    Slot *slot;
    if (ai == NULL || len < 0 || len > MEDIA_AI_CFG_MAX || (len > 0 && cfg == NULL))
    {
        return BASE_ERR;
    }
    pthread_mutex_lock(&ai->mu);
    slot = FindSlot(ai, kind);
    if (slot == NULL || ai->stop)
    {
        pthread_mutex_unlock(&ai->mu);
        return BASE_ERR;
    }
    slot->cfg_len = 0;
    if (len > 0)
    {
        memcpy(slot->cfg, cfg, (size_t)len);
        slot->cfg_len = len;
    }
    pthread_mutex_unlock(&ai->mu);
    return BASE_OK;
}

BaseStatus media_ai_get_config(MediaAi *ai, int kind, void *cfg, int len)
{
    Slot *slot;
    if (ai == NULL || cfg == NULL || len < 0)
    {
        return BASE_ERR;
    }
    pthread_mutex_lock(&ai->mu);
    slot = FindSlot(ai, kind);
    if (slot == NULL || len < slot->cfg_len)
    {
        pthread_mutex_unlock(&ai->mu);
        return BASE_ERR;
    }
    if (slot->cfg_len > 0)
    {
        memcpy(cfg, slot->cfg, (size_t)slot->cfg_len);
    }
    pthread_mutex_unlock(&ai->mu);
    return BASE_OK;
}

static int HasCallback(const MediaAi *ai)
{
    int i;
    for (i = 0; i < MEDIA_AI_SLOT_MAX; ++i)
    {
        if (ai->slot[i].used && ai->slot[i].cb != NULL)
        {
            return 1;
        }
    }
    return 0;
}

static void Take(MediaAi *ai, Job *out)
{
    int best;
    int i;
    best = 0;
    for (i = 1; i < ai->q_count; ++i)
    {
        if (ai->q[i].priority > ai->q[best].priority ||
            (ai->q[i].priority == ai->q[best].priority && ai->q[i].seq < ai->q[best].seq))
        {
            best = i;
        }
    }
    *out = ai->q[best];
    ai->q_count -= 1;
    if (best != ai->q_count)
    {
        ai->q[best] = ai->q[ai->q_count];
    }
}

static int Snapshot(MediaAi *ai, SlotView *view, int cap)
{
    int n;
    int i;
    n = 0;
    for (i = 0; i < MEDIA_AI_SLOT_MAX && n < cap; ++i)
    {
        Slot *slot = &ai->slot[i];
        if (!slot->used || slot->cb == NULL || slot->impl == NULL)
        {
            continue;
        }
        view[n].kind = slot->kind;
        view[n].impl = slot->impl;
        view[n].mod = slot->mod;
        view[n].cb = slot->cb;
        view[n].user = slot->user;
        view[n].cfg_len = slot->cfg_len;
        if (slot->cfg_len > 0)
        {
            memcpy(view[n].cfg, slot->cfg, (size_t)slot->cfg_len);
        }
        view[n].result = slot->result;
        n += 1;
    }
    return n;
}

static void Emit(const SlotView *view, const Job *job, int cb_type, int status, const void *buf, int buf_size)
{
    MediaAiCallbackInfo info;
    memset(&info, 0, sizeof(info));
    info.kind = view->kind;
    info.cb_type = cb_type;
    info.status = status;
    info.buf = buf;
    info.buf_size = buf_size;
    info.param = view->cfg_len > 0 ? view->cfg : NULL;
    info.param_len = view->cfg_len;
    info.tag = job->tag;
    info.user = view->user;
    view->cb(&info);
}

static void RunOne(const SlotView *view, const Job *job)
{
    const BaseImage *frame;
    if (view->mod == NULL)
    {
        Emit(view, job, MEDIA_AI_CB_STATUS, BASE_ERR, NULL, 0);
        return;
    }
    frame = job->has_frame ? &job->frame : NULL;
    if (view->mod->preprocess(view->impl, frame) != BASE_OK ||
        view->mod->infer(view->impl) != BASE_OK ||
        view->mod->postprocess(view->impl, view->result, view->mod->result_size) != BASE_OK)
    {
        Emit(view, job, MEDIA_AI_CB_STATUS, BASE_ERR, NULL, 0);
        return;
    }
    Emit(view, job, MEDIA_AI_CB_DATA, BASE_OK, view->result, view->mod->result_size);
}

static void RunSide(const SlotView *view, const Job *job)
{
    MediaAiInput in;
    int status;
    status = BASE_OK;
    if (view->mod != NULL && view->mod->side != NULL)
    {
        memset(&in, 0, sizeof(in));
        in.type = job->type;
        in.data = job->data;
        in.size = job->size;
        in.priority = job->priority;
        in.exec = job->exec;
        in.tag = job->tag;
        status = view->mod->side(view->impl, &in);
    }
    Emit(view, job, MEDIA_AI_CB_STATUS, status, job->data, job->size);
}

typedef struct
{
    const SlotView *view;
    const Job *job;
}
RunPack;

static void *RunThread(void *arg)
{
    RunPack *pack;
    pack = (RunPack *)arg;
    RunOne(pack->view, pack->job);
    return NULL;
}

static void Execute(const SlotView *view, int n, const Job *job)
{
    int i;
    if (n <= 0)
    {
        return;
    }
    if (job->type != MEDIA_AI_DATA_FRAME)
    {
        for (i = 0; i < n; ++i)
        {
            RunSide(&view[i], job);
        }
        return;
    }
    if (job->exec == MEDIA_AI_EXEC_PARALLEL && n > 1)
    {
        pthread_t th[MEDIA_AI_SLOT_MAX];
        RunPack pack[MEDIA_AI_SLOT_MAX];
        int started[MEDIA_AI_SLOT_MAX];
        for (i = 0; i < n; ++i)
        {
            pack[i].view = &view[i];
            pack[i].job = job;
            started[i] = pthread_create(&th[i], NULL, RunThread, &pack[i]) == 0;
            if (!started[i])
            {
                RunOne(&view[i], job);
            }
        }
        for (i = 0; i < n; ++i)
        {
            if (started[i])
            {
                pthread_join(th[i], NULL);
            }
        }
        return;
    }
    for (i = 0; i < n; ++i)
    {
        RunOne(&view[i], job);
    }
}

static void *Worker(void *arg)
{
    MediaAi *ai;
    ai = (MediaAi *)arg;
    for (;;)
    {
        Job job;
        SlotView view[MEDIA_AI_SLOT_MAX];
        int n;
        pthread_mutex_lock(&ai->mu);
        while (ai->q_count == 0 && !ai->stop)
        {
            pthread_cond_wait(&ai->cv, &ai->mu);
        }
        if (ai->q_count == 0)
        {
            pthread_mutex_unlock(&ai->mu);
            break;
        }
        Take(ai, &job);
        n = Snapshot(ai, view, MEDIA_AI_SLOT_MAX);
        ai->running = 1;
        pthread_mutex_unlock(&ai->mu);
        Execute(view, n, &job);
        pthread_mutex_lock(&ai->mu);
        ai->running = 0;
        pthread_cond_broadcast(&ai->cv);
        pthread_mutex_unlock(&ai->mu);
    }
    return NULL;
}

BaseStatus media_ai_input(MediaAi *ai, const MediaAiInput *in)
{
    Job job;
    if (ai == NULL || in == NULL)
    {
        return BASE_ERR;
    }
    if (in->type < MEDIA_AI_DATA_FRAME || in->type > MEDIA_AI_DATA_AUDIO)
    {
        return BASE_ERR;
    }
    if (in->type == MEDIA_AI_DATA_FRAME)
    {
        if (!base_image_valid(in->frame))
        {
            return BASE_ERR;
        }
    }
    else if (in->size < 0 || (in->size > 0 && in->data == NULL))
    {
        return BASE_ERR;
    }
    memset(&job, 0, sizeof(job));
    job.type = in->type;
    job.priority = in->priority;
    job.exec = in->exec;
    job.data = in->data;
    job.size = in->size;
    job.tag = in->tag;
    if (in->type == MEDIA_AI_DATA_FRAME)
    {
        job.has_frame = 1;
        job.frame = *in->frame;
    }
    pthread_mutex_lock(&ai->mu);
    if (ai->stop || !HasCallback(ai))
    {
        pthread_mutex_unlock(&ai->mu);
        return BASE_ERR;
    }
    if (ai->q_count >= ai->param.queue_depth)
    {
        pthread_mutex_unlock(&ai->mu);
        return BASE_AGAIN;
    }
    job.seq = ai->seq;
    ai->seq += 1;
    ai->q[ai->q_count] = job;
    ai->q_count += 1;
    pthread_cond_signal(&ai->cv);
    pthread_mutex_unlock(&ai->mu);
    return BASE_OK;
}
