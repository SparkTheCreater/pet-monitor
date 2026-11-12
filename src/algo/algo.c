/**
 * @file    algo.c
 * @brief   算法注册表、句柄上的多路算法，以及按队列送入后的回调。
 */
#define _POSIX_C_SOURCE 200809L

#include "algo.h"

#include "algo_internal.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define ALGO_MOD_MAX 16

typedef struct
{
    int used;
    int kind;
    const AlgoModule *mod;
    void *impl;
    AlgoCallback cb;
    void *user;
    unsigned char cfg[ALGO_CFG_MAX];
    int cfg_len;
    unsigned char result[ALGO_RESULT_MAX];
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
    const AlgoModule *mod;
    AlgoCallback cb;
    void *user;
    unsigned char cfg[ALGO_CFG_MAX];
    int cfg_len;
    unsigned char *result;
}
SlotView;

typedef struct
{
    int used;
    const AlgoModule *mod;
    void *so;
    char *path;
    int refs;
}
ModRec;

struct Algo
{
    AlgoOpenParam param;
    Slot slot[ALGO_SLOT_MAX];
    Job q[ALGO_QUEUE_MAX];
    int q_count;
    uint64_t seq;
    int stop;
    int running;
    int worker_on;
    pthread_t worker;
    pthread_mutex_t mu;
    pthread_cond_t cv;
};

static ModRec g_mod[ALGO_MOD_MAX];
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static int ModuleOk(const AlgoModule *mod)
{
    if (mod == NULL || mod->open == NULL || mod->close == NULL)
    {
        return 0;
    }
    if (mod->preprocess == NULL || mod->infer == NULL || mod->postprocess == NULL)
    {
        return 0;
    }
    return mod->result_size > 0 && mod->result_size <= ALGO_RESULT_MAX;
}

static BaseStatus Adopt(const AlgoModule *mod, void *so, const char *path)
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
    for (i = 0; i < ALGO_MOD_MAX; ++i)
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

static const AlgoModule *Retain(int kind)
{
    int i;
    const AlgoModule *mod;
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < ALGO_MOD_MAX; ++i)
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

static void ReleaseMod(const AlgoModule *mod)
{
    int i;
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < ALGO_MOD_MAX; ++i)
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

/* 这四次调用把检测、追踪、识别和猫检测链进来。外部插件走 algo_load。 */
static void LoadBuiltin(void)
{
    ai_detect_register();
    ai_track_register();
    ai_recog_register();
    algo_cat_det_register();
}

static void Ensure(void)
{
    pthread_once(&g_once, LoadBuiltin);
}

BaseStatus algo_register_mod(const AlgoModule *mod)
{
    return Adopt(mod, NULL, NULL);
}

BaseStatus algo_register(const AlgoModule *mod)
{
    Ensure();
    return algo_register_mod(mod);
}

BaseStatus algo_load(const char *path)
{
    void *so;
    const AlgoModule *(*fn)(void);
    const AlgoModule *mod;
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
    fn = (const AlgoModule *(*)(void))dlsym(so, "algo_module");
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

BaseStatus algo_unload(const char *path)
{
    int i;
    void *so;
    char *copy;
    if (path == NULL || path[0] == '\0')
    {
        return BASE_ERR;
    }
    pthread_mutex_lock(&g_mu);
    for (i = 0; i < ALGO_MOD_MAX; ++i)
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

static int Busy(const Algo *ai)
{
    return ai->stop || ai->running || ai->q_count > 0;
}

static int Opening(const Algo *ai)
{
    int i;
    for (i = 0; i < ALGO_SLOT_MAX; ++i)
    {
        if (ai->slot[i].used && ai->slot[i].impl == NULL)
        {
            return 1;
        }
    }
    return 0;
}

static int HasKind(const Algo *ai, int kind)
{
    int i;
    for (i = 0; i < ALGO_SLOT_MAX; ++i)
    {
        if (ai->slot[i].used && ai->slot[i].kind == kind)
        {
            return 1;
        }
    }
    return 0;
}

static Slot *FindSlot(Algo *ai, int kind)
{
    int i;
    for (i = 0; i < ALGO_SLOT_MAX; ++i)
    {
        if (ai->slot[i].used && ai->slot[i].impl != NULL && ai->slot[i].kind == kind)
        {
            return &ai->slot[i];
        }
    }
    return NULL;
}

BaseStatus algo_open(const AlgoOpenParam *param, Algo **out)
{
    Algo *ai;
    int depth;
    if (out != NULL)
    {
        *out = NULL;
    }
    if (out == NULL)
    {
        return BASE_ERR;
    }
    depth = ALGO_QUEUE_DEFAULT;
    if (param != NULL)
    {
        if (param->width < 0 || param->height < 0 || param->queue_depth < 0)
        {
            return BASE_ERR;
        }
        if (param->queue_depth > ALGO_QUEUE_MAX)
        {
            return BASE_ERR;
        }
        if (param->queue_depth > 0)
        {
            depth = param->queue_depth;
        }
    }
    Ensure();
    ai = (Algo *)calloc(1, sizeof(*ai));
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

void algo_close(Algo *ai)
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
    for (i = 0; i < ALGO_SLOT_MAX; ++i)
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

BaseStatus algo_get_param(Algo *ai, AlgoOpenParam *out)
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

BaseStatus algo_add(Algo *ai, int kind, const char *model_path)
{
    const AlgoModule *mod;
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
    for (i = 0; i < ALGO_SLOT_MAX; ++i)
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

BaseStatus algo_set_callback(Algo *ai, int kind, AlgoCallback cb, void *user)
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

BaseStatus algo_set_config(Algo *ai, int kind, const void *cfg, int len)
{
    Slot *slot;
    if (ai == NULL || len < 0 || len > ALGO_CFG_MAX || (len > 0 && cfg == NULL))
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

BaseStatus algo_get_config(Algo *ai, int kind, void *cfg, int len)
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

static int HasCallback(const Algo *ai)
{
    int i;
    for (i = 0; i < ALGO_SLOT_MAX; ++i)
    {
        if (ai->slot[i].used && ai->slot[i].cb != NULL)
        {
            return 1;
        }
    }
    return 0;
}

static void Take(Algo *ai, Job *out)
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

static int Snapshot(Algo *ai, SlotView *view, int cap)
{
    int n;
    int i;
    n = 0;
    for (i = 0; i < ALGO_SLOT_MAX && n < cap; ++i)
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
    AlgoCallbackInfo info;
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
        Emit(view, job, ALGO_CB_STATUS, BASE_ERR, NULL, 0);
        return;
    }
    frame = job->has_frame ? &job->frame : NULL;
    if (view->mod->preprocess(view->impl, frame) != BASE_OK ||
        view->mod->infer(view->impl) != BASE_OK ||
        view->mod->postprocess(view->impl, view->result, view->mod->result_size) != BASE_OK)
    {
        Emit(view, job, ALGO_CB_STATUS, BASE_ERR, NULL, 0);
        return;
    }
    Emit(view, job, ALGO_CB_DATA, BASE_OK, view->result, view->mod->result_size);
}

static void RunSide(const SlotView *view, const Job *job)
{
    AlgoInput in;
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
    Emit(view, job, ALGO_CB_STATUS, status, job->data, job->size);
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
    if (job->type != ALGO_DATA_FRAME)
    {
        for (i = 0; i < n; ++i)
        {
            RunSide(&view[i], job);
        }
        return;
    }
    if (job->exec == ALGO_EXEC_PARALLEL && n > 1)
    {
        pthread_t th[ALGO_SLOT_MAX];
        RunPack pack[ALGO_SLOT_MAX];
        int started[ALGO_SLOT_MAX];
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
    Algo *ai;
    ai = (Algo *)arg;
    for (;;)
    {
        Job job;
        SlotView view[ALGO_SLOT_MAX];
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
        n = Snapshot(ai, view, ALGO_SLOT_MAX);
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

BaseStatus algo_input(Algo *ai, const AlgoInput *in)
{
    Job job;
    if (ai == NULL || in == NULL)
    {
        return BASE_ERR;
    }
    if (in->type < ALGO_DATA_FRAME || in->type > ALGO_DATA_AUDIO)
    {
        return BASE_ERR;
    }
    if (in->type == ALGO_DATA_FRAME)
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
    if (in->type == ALGO_DATA_FRAME)
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
