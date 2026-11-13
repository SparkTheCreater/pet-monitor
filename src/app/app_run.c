/**
 * @file    app_run.c
 * @brief   一路相机一条线程。判断在本线程，发信只交队列。
 */
#define _POSIX_C_SOURCE 200809L

#include "app_run.h"

#include "algo.h"
#include "algo_cfg.h"
#include "app_cfg.h"
#include "app_mail.h"
#include "app_mark.h"
#include "app_watch.h"
#include "base_path.h"
#include "media_cfg.h"
#include "media_dec.h"
#include "media_jpeg.h"
#include "media_mem.h"
#include "media_sys.h"
#include "net_cfg.h"
#include "net_pull.h"

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const int kCoreMask[3] = {1, 2, 4};

static atomic_int g_stop;

typedef struct
{
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int done;
    int cb_type;
    int status;
    AlgoDetectResult result;
}
InferWait;

typedef struct
{
    const AppConfig *cfg;
    const AppCamera *camera;
    int transport;
    int core;
    const AlgoConfig *algo;
    int jpeg_quality;
    AppMail *mail;
    pthread_mutex_t mu;
    NetPull *pull;
    pthread_t thread;
    int on;
}
Lane;

static int64_t NowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void OnStop(int signo)
{
    (void)signo;
    atomic_store(&g_stop, 1);
}

static void OnCat(const AlgoCallbackInfo *info)
{
    InferWait *wait = (InferWait *)info->user;
    pthread_mutex_lock(&wait->mu);
    wait->cb_type = info->cb_type;
    wait->status = info->status;
    memset(&wait->result, 0, sizeof(wait->result));
    if (info->cb_type == ALGO_CB_DATA && info->buf != NULL && info->buf_size >= (int)sizeof(AlgoDetectResult))
    {
        memcpy(&wait->result, info->buf, sizeof(wait->result));
    }
    wait->done = 1;
    pthread_cond_signal(&wait->cv);
    pthread_mutex_unlock(&wait->mu);
}

static int WaitInfer(InferWait *wait)
{
    struct timespec ts;
    int ok;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;
    ok = 1;
    pthread_mutex_lock(&wait->mu);
    while (!wait->done)
    {
        if (pthread_cond_timedwait(&wait->cv, &wait->mu, &ts) != 0)
        {
            ok = 0;
            break;
        }
    }
    pthread_mutex_unlock(&wait->mu);
    return ok;
}

static int Infer(Algo *ai, InferWait *wait, const BaseImage *frame, AlgoDetectResult *result)
{
    AlgoInput in;
    int spins;
    memset(&in, 0, sizeof(in));
    in.type = ALGO_DATA_FRAME;
    in.frame = frame;
    in.exec = ALGO_EXEC_SERIAL;
    for (spins = 0; spins < 40; spins++)
    {
        BaseStatus status;
        pthread_mutex_lock(&wait->mu);
        wait->done = 0;
        pthread_mutex_unlock(&wait->mu);
        status = algo_input(ai, &in);
        if (status == BASE_AGAIN)
        {
            struct timespec pause;
            pause.tv_sec = 0;
            pause.tv_nsec = 10000000L;
            nanosleep(&pause, NULL);
            continue;
        }
        if (status != BASE_OK || !WaitInfer(wait))
        {
            return -1;
        }
        if (wait->cb_type != ALGO_CB_DATA || wait->status != BASE_OK)
        {
            return -1;
        }
        *result = wait->result;
        return 0;
    }
    return -1;
}

static void Apply(Lane *lane, AppWatch *watch, uint8_t **jpeg, size_t *jpeg_len, const AppWatchEvent *ev, BaseImage *frame, const AlgoDetectResult *result)
{
    uint8_t *encoded;
    size_t encoded_len;
    if (ev->drop)
    {
        media_jpeg_free(*jpeg);
        *jpeg = NULL;
        *jpeg_len = 0;
    }
    if (ev->keep)
    {
        BaseImage marked;
        encoded = NULL;
        encoded_len = 0;
        memset(&marked, 0, sizeof(marked));
        marked.fd = -1;
        if (app_mark_cats(frame, result, &marked) == BASE_OK &&
            media_jpeg_encode(&marked, lane->jpeg_quality, &encoded, &encoded_len) == BASE_OK)
        {
            media_jpeg_free(*jpeg);
            *jpeg = encoded;
            *jpeg_len = encoded_len;
            app_watch_kept(watch);
        }
        media_mem_free(&marked);
    }
    if (ev->send && *jpeg != NULL)
    {
        fprintf(stderr, "[app] %s latched\n", lane->camera->name);
        if (app_mail_post(lane->mail, lane->camera->name, *jpeg, *jpeg_len) != BASE_OK)
        {
            *jpeg = NULL;
            *jpeg_len = 0;
            return;
        }
        *jpeg = NULL;
        *jpeg_len = 0;
    }
}

static int UseFrame(Lane *lane, Algo *ai, InferWait *wait, AppWatch *watch, MediaDec *dec, uint8_t **jpeg, size_t *jpeg_len, BaseDecodedFrame *frame)
{
    AppWatchEvent ev;
    AppWatchParam param;
    AlgoDetectResult result;
    int rc;
    if (frame->eos || frame->image.ptr == NULL || frame->image.width <= 0)
    {
        return media_dec_release(dec, frame) == BASE_OK ? 0 : -1;
    }
    memset(&result, 0, sizeof(result));
    rc = Infer(ai, wait, &frame->image, &result);
    if (rc == 0)
    {
        param.present_sec = lane->cfg->present_sec;
        param.shot_sec = lane->cfg->shot_sec;
        param.absent_sec = lane->cfg->absent_sec;
        app_watch_step(watch, &param, NowMs(), result.count > 0, &ev);
        Apply(lane, watch, jpeg, jpeg_len, &ev, &frame->image, &result);
    }
    if (media_dec_release(dec, frame) != BASE_OK)
    {
        return -1;
    }
    return rc;
}

static int Drain(Lane *lane, Algo *ai, InferWait *wait, AppWatch *watch, MediaDec *dec, uint8_t **jpeg, size_t *jpeg_len)
{
    for (;;)
    {
        BaseDecodedFrame frame;
        BaseStatus status;
        memset(&frame, 0, sizeof(frame));
        status = media_dec_receive(dec, &frame);
        if (status == BASE_AGAIN)
        {
            return 0;
        }
        if (status != BASE_OK)
        {
            return -1;
        }
        if (UseFrame(lane, ai, wait, watch, dec, jpeg, jpeg_len, &frame) != 0)
        {
            return -1;
        }
    }
}

static BaseStatus SendPkt(Lane *lane, Algo *ai, InferWait *wait, AppWatch *watch, MediaDec *dec, uint8_t **jpeg, size_t *jpeg_len, const uint8_t *data, int len, int64_t pts, int eos)
{
    int spins;
    for (spins = 0; spins < 200; spins++)
    {
        BaseStatus status = media_dec_send(dec, data, len, pts, eos);
        struct timespec pause;
        if (status == BASE_OK)
        {
            return BASE_OK;
        }
        if (status != BASE_AGAIN || Drain(lane, ai, wait, watch, dec, jpeg, jpeg_len) != 0)
        {
            return status == BASE_AGAIN ? BASE_ERR : status;
        }
        pause.tv_sec = 0;
        pause.tv_nsec = 10000000L;
        nanosleep(&pause, NULL);
    }
    return BASE_ERR;
}

static void Publish(Lane *lane, NetPull *pull)
{
    pthread_mutex_lock(&lane->mu);
    lane->pull = pull;
    pthread_mutex_unlock(&lane->mu);
}

static NetPull *TakePull(Lane *lane)
{
    NetPull *pull;
    pthread_mutex_lock(&lane->mu);
    pull = lane->pull;
    lane->pull = NULL;
    pthread_mutex_unlock(&lane->mu);
    return pull;
}

static void ClosePull(NetPull *pull)
{
    if (pull != NULL)
    {
        net_close(pull);
    }
}

static void SleepRetry(void)
{
    struct timespec pause;
    int i;
    for (i = 0; i < 20 && !atomic_load(&g_stop); i++)
    {
        pause.tv_sec = 0;
        pause.tv_nsec = 100000000L;
        nanosleep(&pause, NULL);
    }
}

static void RunOnce(Lane *lane)
{
    NetSource source;
    NetPull *pull;
    MediaDec *dec;
    Algo *ai;
    InferWait wait;
    AppWatch watch;
    AlgoOpenParam param;
    NetInfo info;
    uint8_t *jpeg;
    size_t jpeg_len;
    int saw_key;
    memset(&source, 0, sizeof(source));
    snprintf(source.name, sizeof(source.name), "%s", lane->camera->name);
    snprintf(source.url, sizeof(source.url), "%s", lane->camera->url);
    source.timeout_ms = lane->camera->timeout_ms;
    pull = NULL;
    if (net_open(&source, lane->transport, &pull) != BASE_OK)
    {
        fprintf(stderr, "[app] %s open failed\n", lane->camera->name);
        return;
    }
    Publish(lane, pull);
    if (atomic_load(&g_stop))
    {
        ClosePull(TakePull(lane));
        return;
    }
    memset(&info, 0, sizeof(info));
    dec = NULL;
    ai = NULL;
    jpeg = NULL;
    jpeg_len = 0;
    saw_key = 0;
    memset(&wait, 0, sizeof(wait));
    app_watch_reset(&watch);
    if (pthread_mutex_init(&wait.mu, NULL) != 0 || pthread_cond_init(&wait.cv, NULL) != 0)
    {
        ClosePull(TakePull(lane));
        return;
    }
    if (net_info(pull, &info) != BASE_OK ||
        media_dec_open(info.codec, 12, info.extradata, (size_t)info.extradata_size, &dec) != BASE_OK)
    {
        goto done;
    }
    memset(&param, 0, sizeof(param));
    param.dev_id = lane->core;
    param.queue_depth = 1;
    param.score_min = app_cfg_camera_score(lane->camera);
    {
        int kind = app_cfg_camera_algo(lane->camera);
        const char *rel = algo_cfg_model(lane->algo, kind);
        char path[APP_PATH_MAX];
        if (rel == NULL || base_path_join(lane->cfg->dir, rel, path, sizeof(path)) != BASE_OK ||
            algo_open(&param, &ai) != BASE_OK || algo_add(ai, kind, path) != BASE_OK ||
            algo_set_callback(ai, kind, OnCat, &wait) != BASE_OK)
        {
            fprintf(stderr, "[app] %s algo failed\n", lane->camera->name);
            goto done;
        }
        fprintf(stderr, "[app] %s algo %d score %.2f\n", lane->camera->name, kind, (double)param.score_min);
    }
    while (!atomic_load(&g_stop))
    {
        NetPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        if (net_read(pull, &pkt) != BASE_OK)
        {
            break;
        }
        if (!saw_key)
        {
            if (!pkt.key)
            {
                break;
            }
            saw_key = 1;
        }
        if (SendPkt(lane, ai, &wait, &watch, dec, &jpeg, &jpeg_len, pkt.data, pkt.size, pkt.pts_us, 0) != BASE_OK ||
            Drain(lane, ai, &wait, &watch, dec, &jpeg, &jpeg_len) != 0)
        {
            break;
        }
    }
done:
    media_jpeg_free(jpeg);
    if (ai != NULL)
    {
        algo_close(ai);
    }
    if (dec != NULL)
    {
        media_dec_close(dec);
    }
    ClosePull(TakePull(lane));
    pthread_cond_destroy(&wait.cv);
    pthread_mutex_destroy(&wait.mu);
}

static void *LaneMain(void *arg)
{
    Lane *lane = arg;
    while (!atomic_load(&g_stop))
    {
        RunOnce(lane);
        if (!atomic_load(&g_stop))
        {
            SleepRetry();
        }
    }
    return NULL;
}

static int CoreOf(int index)
{
    if (index < 0)
    {
        return kCoreMask[0];
    }
    return kCoreMask[index % 3];
}

static void StopLanes(Lane *lane, int n)
{
    int i;
    atomic_store(&g_stop, 1);
    for (i = 0; i < n; i++)
    {
        ClosePull(TakePull(&lane[i]));
    }
}

BaseStatus app_run(const char *ini_path)
{
    BaseIni *ini;
    AppConfig app;
    NetConfig net;
    AlgoConfig algo;
    MediaConfig media;
    AppMail *mail;
    Lane lane[APP_CAMERA_MAX];
    struct sigaction action;
    int i;
    int started;
    if (ini_path == NULL || ini_path[0] == '\0')
    {
        return BASE_ERR;
    }
    atomic_store(&g_stop, 0);
    ini = base_ini_load(ini_path);
    if (ini == NULL || app_cfg_from_ini(ini, &app) != BASE_OK || net_cfg_from_ini(ini, &net) != BASE_OK ||
        algo_cfg_from_ini(ini, &algo) != BASE_OK || media_cfg_from_ini(ini, &media) != BASE_OK)
    {
        base_ini_free(ini);
        return BASE_ERR;
    }
    if (app.camera_count <= 0)
    {
        base_ini_free(ini);
        return BASE_ERR;
    }
    if (media_init() != BASE_OK)
    {
        base_ini_free(ini);
        return BASE_ERR;
    }
    mail = NULL;
    if (app_mail_open(&app, &mail) != BASE_OK)
    {
        media_deinit();
        base_ini_free(ini);
        return BASE_ERR;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = OnStop;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    memset(lane, 0, sizeof(lane));
    started = 0;
    for (i = 0; i < app.camera_count; i++)
    {
        lane[i].cfg = &app;
        lane[i].camera = &app.camera[i];
        lane[i].transport = net.transport;
        lane[i].core = CoreOf(i);
        lane[i].algo = &algo;
        lane[i].jpeg_quality = media.jpeg_quality;
        lane[i].mail = mail;
        if (pthread_mutex_init(&lane[i].mu, NULL) != 0)
        {
            break;
        }
        if (pthread_create(&lane[i].thread, NULL, LaneMain, &lane[i]) != 0)
        {
            pthread_mutex_destroy(&lane[i].mu);
            break;
        }
        lane[i].on = 1;
        started += 1;
    }
    if (started == app.camera_count)
    {
        while (!atomic_load(&g_stop))
        {
            struct timespec pause;
            pause.tv_sec = 0;
            pause.tv_nsec = 200000000L;
            nanosleep(&pause, NULL);
        }
    }
    StopLanes(lane, started);
    for (i = 0; i < started; i++)
    {
        if (lane[i].on)
        {
            pthread_join(lane[i].thread, NULL);
            pthread_mutex_destroy(&lane[i].mu);
        }
    }
    app_mail_close(mail);
    media_deinit();
    base_ini_free(ini);
    return started == app.camera_count ? BASE_OK : BASE_ERR;
}
