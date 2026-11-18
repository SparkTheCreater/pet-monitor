/**
 * @file    app_run.c
 * @brief   一路相机一条线程。判断函数给出要不要做，执行函数为真才做。
 */
#define _POSIX_C_SOURCE 200809L

#include "app_run.h"

#include "algo.h"
#include "algo_cfg.h"
#include "app_act.h"
#include "app_cfg.h"
#include "app_mail.h"
#include "app_watch.h"
#include "base_path.h"
#include "media_cfg.h"
#include "media_dec.h"
#include "media_jpeg.h"
#include "media_sys.h"
#include "net_cfg.h"
#include "net_pull.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const int kCoreMask[3] = {1, 2, 4};

static atomic_int g_stop;
static AppRunJudge g_judge;
static int g_judge_on;
static AppRunAct g_act;
static int g_act_on;

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
    int draw_box;
    int draw_score;
    AppMail *mail;
    AppWatchJudge stay;
    AppJudgeFn judge;
    void *judge_user;
    AppJudgeKeptFn kept;
    void *kept_user;
    void (*reset)(void *user);
    AppActFn act;
    void *act_user;
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

static void Act(Lane *lane, const AppFact *fact, const AppVerdict *verdict, BaseImage *frame, uint8_t **jpeg, size_t *jpeg_len)
{
    AppShot shot;
    if (lane->act == NULL)
    {
        return;
    }
    memset(&shot, 0, sizeof(shot));
    shot.fact = fact;
    shot.frame = frame;
    shot.jpeg = jpeg;
    shot.jpeg_len = jpeg_len;
    shot.jpeg_quality = lane->jpeg_quality;
    shot.draw_box = lane->draw_box;
    shot.draw_score = lane->draw_score;
    shot.kept = lane->kept;
    shot.kept_user = lane->kept_user;
    shot.mail = lane->mail;
    shot.dir = lane->cfg->shot_dir;
    lane->act(&shot, verdict, lane->act_user);
}

static int InRoi(const AppCamera *camera, const AlgoDetectResult *result)
{
    int i;
    int n;
    int kind;
    if (result == NULL)
    {
        return 0;
    }
    kind = app_cfg_camera_algo(camera);
    if (kind != ALGO_CAT_DET && kind != ALGO_DETECT)
    {
        return result->count;
    }
    n = 0;
    for (i = 0; i < result->count && i < ALGO_MAX; i++)
    {
        if (app_cfg_box_inside(camera, &result->box[i].box))
        {
            n += 1;
        }
    }
    return n;
}

static void LogSeen(const AppFact *fact, const AppVerdict *verdict)
{
    struct tm tm;
    char stamp[24];
    time_t sec;
    sec = time(NULL) + 8 * 3600;
    gmtime_r(&sec, &tm);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(stderr, "[app] %s %s cats=%d roi=%d keep=%d send=%d\n",
            stamp,
            fact->camera != NULL ? fact->camera->name : "cam",
            fact->cats,
            fact->roi_cats,
            verdict->keep,
            verdict->send);
}

static int UseFrame(Lane *lane, Algo *ai, InferWait *wait, MediaDec *dec, uint8_t **jpeg, size_t *jpeg_len, BaseDecodedFrame *frame)
{
    AppFact fact;
    AppVerdict verdict;
    AlgoDetectResult result;
    int rc;
    if (frame->eos || frame->image.ptr == NULL || frame->image.width <= 0)
    {
        return media_dec_release(dec, frame) == BASE_OK ? 0 : -1;
    }
    memset(&result, 0, sizeof(result));
    rc = Infer(ai, wait, &frame->image, &result);
    if (rc == 0 && lane->judge != NULL)
    {
        memset(&fact, 0, sizeof(fact));
        memset(&verdict, 0, sizeof(verdict));
        fact.now_ms = NowMs();
        fact.cats = result.count;
        fact.roi_cats = InRoi(lane->camera, &result);
        fact.has_cat = fact.roi_cats > 0;
        fact.camera = lane->camera;
        fact.result = &result;
        lane->judge(&fact, lane->judge_user, &verdict);
        if (fact.cats > 0)
        {
            LogSeen(&fact, &verdict);
        }
        Act(lane, &fact, &verdict, &frame->image, jpeg, jpeg_len);
    }
    if (media_dec_release(dec, frame) != BASE_OK)
    {
        return -1;
    }
    return rc;
}

static int Drain(Lane *lane, Algo *ai, InferWait *wait, MediaDec *dec, uint8_t **jpeg, size_t *jpeg_len)
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
        if (UseFrame(lane, ai, wait, dec, jpeg, jpeg_len, &frame) != 0)
        {
            return -1;
        }
    }
}

static BaseStatus SendPkt(Lane *lane, Algo *ai, InferWait *wait, MediaDec *dec, uint8_t **jpeg, size_t *jpeg_len, const uint8_t *data, int len, int64_t pts, int eos)
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
        if (status != BASE_AGAIN || Drain(lane, ai, wait, dec, jpeg, jpeg_len) != 0)
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
    if (lane->reset != NULL)
    {
        lane->reset(lane->judge_user);
    }
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
    param.nms_iou = lane->algo->nms_iou;
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
        if (SendPkt(lane, ai, &wait, dec, &jpeg, &jpeg_len, pkt.data, pkt.size, pkt.pts_us, 0) != BASE_OK ||
            Drain(lane, ai, &wait, dec, &jpeg, &jpeg_len) != 0)
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

static void UseDefault(Lane *lane)
{
    lane->stay.param.present_sec = lane->cfg->present_sec;
    lane->stay.param.shot_sec = lane->cfg->shot_sec;
    lane->stay.param.pair_sec = lane->cfg->pair_sec;
    lane->stay.param.cooldown_sec = lane->cfg->cooldown_sec;
    lane->judge = app_watch_judge;
    lane->judge_user = &lane->stay;
    lane->kept = app_watch_judge_kept;
    lane->kept_user = &lane->stay;
    lane->reset = app_watch_judge_reset;
    lane->act = app_act_run;
    lane->act_user = NULL;
}

static void UseHooks(Lane *lane, int index)
{
    UseDefault(lane);
    if (g_judge_on)
    {
        lane->judge = g_judge.judge;
        lane->kept = g_judge.kept;
        lane->reset = g_judge.reset;
        lane->judge_user = NULL;
        lane->kept_user = NULL;
        if (g_judge.open != NULL)
        {
            lane->judge_user = g_judge.open(index, lane->camera, g_judge.user);
            lane->kept_user = lane->judge_user;
        }
    }
    if (g_act_on)
    {
        lane->act = g_act.act;
        lane->act_user = g_act.user;
    }
}

void app_run_set_judge(const AppRunJudge *judge)
{
    if (judge == NULL || judge->judge == NULL)
    {
        g_judge_on = 0;
        memset(&g_judge, 0, sizeof(g_judge));
        return;
    }
    g_judge = *judge;
    g_judge_on = 1;
}

void app_run_set_act(const AppRunAct *act)
{
    if (act == NULL || act->act == NULL)
    {
        g_act_on = 0;
        memset(&g_act, 0, sizeof(g_act));
        return;
    }
    g_act = *act;
    g_act_on = 1;
}

static void EnsureDir(const char *path)
{
    if (path == NULL || path[0] == '\0')
    {
        return;
    }
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
    {
        return;
    }
    fprintf(stderr, "[app] shot dir %s\n", path);
}

static void OpenLog(const char *path)
{
    if (path == NULL || path[0] == '\0')
    {
        return;
    }
    if (freopen(path, "a", stderr) == NULL)
    {
        fprintf(stderr, "[app] log file %s\n", path);
    }
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
    OpenLog(app.log_file);
    EnsureDir(app.shot_dir);
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
        lane[i].draw_box = media.draw_box;
        lane[i].draw_score = media.draw_score;
        lane[i].mail = mail;
        UseHooks(&lane[i], i);
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
