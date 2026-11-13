/**
 * @file    ini_test.c
 * @brief   ini 解析，以及媒体、算法、应用各自只读自己的段。
 */
#include "algo_cfg.h"
#include "app_cfg.h"
#include "base_path.h"
#include "ini.h"
#include "media_cfg.h"

#include <stdio.h>
#include <string.h>

static int g_failed = 0;

static void ExpectStatus(BaseStatus got, BaseStatus want, const char *name)
{
    if (got == want)
    {
        return;
    }
    fprintf(stderr, "%s got %d want %d\n", name, (int)got, (int)want);
    g_failed = 1;
}

static void ExpectInt(int got, int want, const char *name)
{
    if (got == want)
    {
        return;
    }
    fprintf(stderr, "%s got %d want %d\n", name, got, want);
    g_failed = 1;
}

static void ExpectStr(const char *got, const char *want, const char *name)
{
    if (got != NULL && want != NULL && strcmp(got, want) == 0)
    {
        return;
    }
    fprintf(stderr, "%s got \"%s\"\n", name, got != NULL ? got : "(null)");
    g_failed = 1;
}

static void ExpectNear(float got, float want, const char *name)
{
    float diff = got - want;
    if (diff < 0.0f)
    {
        diff = -diff;
    }
    if (diff < 0.0001f)
    {
        return;
    }
    fprintf(stderr, "%s got %f want %f\n", name, (double)got, (double)want);
    g_failed = 1;
}

static void ExpectUntouched(unsigned char fill, const unsigned char *p, const char *name)
{
    if (*p == fill)
    {
        return;
    }
    fprintf(stderr, "%s was written\n", name);
    g_failed = 1;
}

static void TestParse(void)
{
    BaseIni *ini;
    int n = 0;
    int bad = 0;
    const char *names[4];
    ini = base_ini_parse(NULL);
    if (ini != NULL)
    {
        fprintf(stderr, "parse null returned a handle\n");
        g_failed = 1;
        base_ini_free(ini);
    }
    base_ini_free(NULL);

    ini = base_ini_parse(
        "\xEF\xBB\xBF"
        "; comment\n"
        "[camera]\r\n"
        "timeout_ms = 5000\r\n"
        "\n"
        "[camera.food]\n"
        "url = rtsp://a ; door\n"
        "place = eat\n"
        "[camera.litter]\n"
        "url = \"rtsp://b;c\"\n"
        "place = litter\n"
        "timeout_ms = 1000\n"
        "[n]\n"
        "a = -2147483648\n"
        "b = 2147483648\n"
        "flag = YES\n"
        "empty = \n"
        "pass = \"a;b c\"\n"
        "score = 0.25\n"
        "score = 0.5\n");
    if (ini == NULL)
    {
        fprintf(stderr, "parse sample text failed\n");
        g_failed = 1;
        return;
    }
    ExpectInt(base_ini_sections(ini, "camera.", names, 4), 2, "camera count");
    ExpectStr(names[0], "camera.food", "first camera");
    ExpectStr(names[1], "camera.litter", "second camera");
    ExpectStr(base_ini_str(ini, "camera.food", "url", NULL), "rtsp://a", "inline comment");
    ExpectStr(base_ini_str(ini, "camera.litter", "url", NULL), "rtsp://b;c", "quoted semi");
    ExpectStr(base_ini_str(ini, "n", "pass", NULL), "a;b c", "quoted spaces");
    ExpectStr(base_ini_str(ini, "n", "empty", "x"), "", "empty value");
    ExpectInt(base_ini_has(ini, "n", "empty"), 1, "empty is present");
    ExpectInt(base_ini_has(ini, "n", "missing"), 0, "missing key");
    ExpectStatus(base_ini_int(ini, "n", "a", 0, &n), BASE_OK, "int min");
    ExpectInt(n, -2147483647 - 1, "int min value");
    ExpectStatus(base_ini_int(ini, "n", "b", 3, &bad), BASE_ERR, "int overflow");
    ExpectInt(bad, 3, "overflow keeps default");
    ExpectStatus(base_ini_bool(ini, "n", "flag", 0, &n), BASE_OK, "bool yes");
    ExpectInt(n, 1, "bool value");
    ExpectStatus(base_ini_int(ini, "n", "nope", 4, &n), BASE_OK, "missing int");
    ExpectInt(n, 4, "missing int value");
    ExpectStatus(base_ini_real(ini, "n", "score", 0.0, NULL), BASE_ERR, "null out");
    {
        double score = 0.0;
        ExpectStatus(base_ini_real(ini, "n", "score", 0.0, &score), BASE_OK, "last real");
        if (score < 0.499 || score > 0.501)
        {
            fprintf(stderr, "last key got %f\n", score);
            g_failed = 1;
        }
    }
    base_ini_free(ini);

    if (base_ini_parse("fps = 1\n") != NULL)
    {
        fprintf(stderr, "key before section was accepted\n");
        g_failed = 1;
    }
    if (base_ini_parse("[camera\nurl = x\n") != NULL)
    {
        fprintf(stderr, "unclosed section was accepted\n");
        g_failed = 1;
    }
    if (base_ini_parse("[a]\nkey = \"ab\n") != NULL)
    {
        fprintf(stderr, "unclosed quote was accepted\n");
        g_failed = 1;
    }
    if (base_ini_parse("[a]\nkey = \"\\q\"\n") != NULL)
    {
        fprintf(stderr, "bad escape was accepted\n");
        g_failed = 1;
    }
    if (base_ini_load("/no/such/pet-monitor.ini") != NULL)
    {
        fprintf(stderr, "missing file returned a handle\n");
        g_failed = 1;
    }
}

static void TestPath(void)
{
    char out[64];
    unsigned char fill = 0x5a;
    out[0] = (char)fill;
    ExpectStatus(base_path_join("/opt/pet", "models/a.rknn", out, sizeof(out)), BASE_OK, "join");
    ExpectStr(out, "/opt/pet/models/a.rknn", "joined");
    ExpectStatus(base_path_join("/opt/pet/", "models/a.rknn", out, sizeof(out)), BASE_OK, "join slash");
    ExpectStr(out, "/opt/pet/models/a.rknn", "joined slash");
    ExpectStatus(base_path_join("/opt/pet", "/abs/a", out, sizeof(out)), BASE_OK, "absolute");
    ExpectStr(out, "/abs/a", "absolute path");
    out[0] = (char)fill;
    ExpectStatus(base_path_join("/opt/pet", "models/a.rknn", out, 4), BASE_ERR, "join overflow");
    ExpectUntouched(fill, (unsigned char *)out, "join overflow");
}

static void TestLayers(void)
{
    BaseIni *ini;
    BaseIni *empty;
    MediaConfig media;
    AlgoConfig algo;
    AppConfig app;
    char joined[APP_DIR_MAX + ALGO_PATH_MAX];
    unsigned char media_fill[sizeof(MediaConfig)];
    ini = base_ini_load(PM_INI_SAMPLE);
    if (ini == NULL)
    {
        fprintf(stderr, "cannot load %s\n", PM_INI_SAMPLE);
        g_failed = 1;
        return;
    }
    ExpectInt(base_ini_sections(ini, "", NULL, 0), 8, "sample sections");
    ExpectStatus(media_cfg_from_ini(ini, &media), BASE_OK, "media");
    ExpectInt(media.enc.codec, BASE_CODEC_H264, "codec");
    ExpectInt(media.enc.format, BASE_PIX_NV12, "format");
    ExpectInt(media.enc.width, 1920, "width");
    ExpectInt(media.enc.height, 1080, "height");
    ExpectInt(media.enc.fps, 25, "fps");
    ExpectInt(media.enc.gop, 50, "gop");
    ExpectInt(media.enc.rc_mode, MEDIA_RC_CBR, "rc");
    ExpectInt(media.enc.bitrate, 2000000, "bitrate");
    ExpectInt(media.enc.qp_init, 26, "qp init");
    ExpectInt(media.enc.qp_min, 10, "qp min");
    ExpectInt(media.enc.qp_max, 51, "qp max");
    ExpectInt(media.enc.profile, 100, "profile");
    ExpectInt(media.enc.level, 40, "level");
    ExpectInt(media.jpeg_quality, 80, "jpeg");

    ExpectStatus(algo_cfg_from_ini(ini, &algo), BASE_OK, "algo");
    ExpectStr(algo.detect_model, "models/yolov5s-640-640.rknn", "detect model");
    ExpectStr(algo.track_model, "models/yolov5s-640-640.rknn", "track model");
    ExpectStr(algo.cat_model, "models/Catto.rknn", "cat model");
    ExpectNear(algo.score_min, 0.25f, "score");
    ExpectNear(algo.nms_iou, 0.45f, "nms");
    ExpectInt(algo.open.queue_depth, 2, "queue");
    ExpectInt(algo.open.dev_id, 0, "dev");
    ExpectInt(algo.open.width, 0, "algo width");

    ExpectStatus(app_cfg_from_ini(ini, &app), BASE_OK, "app");
    ExpectStr(app.dir, "/opt/pet-monitor", "dir");
    ExpectInt(app.camera_count, 2, "cameras");
    ExpectStr(app.camera[0].name, "food", "food name");
    ExpectStr(app.camera[0].url, "rtsp://192.168.1.20:554/stream1", "food url");
    ExpectInt(app.camera[0].place, APP_PLACE_EAT, "food place");
    ExpectInt(app.camera[0].timeout_ms, 5000, "food timeout");
    ExpectStr(app.camera[1].name, "litter", "litter name");
    ExpectStr(app.camera[1].url, "rtsp://192.168.1.21:554/stream1", "litter url");
    ExpectInt(app.camera[1].place, APP_PLACE_LITTER, "litter place");
    ExpectInt(app.camera[1].timeout_ms, 5000, "litter timeout");
    ExpectInt(app.eat_sec, 3, "eat");
    ExpectInt(app.litter_sec, 5, "litter sec");
    ExpectInt(app.clip_sec, 10, "clip");
    ExpectInt(app.email_enable, 0, "mail off");
    ExpectStr(app.host, "smtp.qq.com", "host");
    ExpectStr(app.user, "", "user");
    ExpectStr(app.from, "", "from");
    ExpectInt(app.email.port, 465, "port");
    ExpectInt(app.email.tls, APP_EMAIL_TLS_IMPLICIT, "tls");
    ExpectInt(app.email.insecure, 0, "insecure");
    ExpectInt(app.email.timeout_ms, 30000, "mail timeout");
    if (app.email.host != app.host || app.email.username != app.user || app.email.ca_file != NULL)
    {
        fprintf(stderr, "email pointers\n");
        g_failed = 1;
    }
    ExpectStatus(base_path_join(app.dir, algo.cat_model, joined, sizeof(joined)), BASE_OK, "model path");
    ExpectStr(joined, "/opt/pet-monitor/models/Catto.rknn", "model joined");
    base_ini_free(ini);

    empty = base_ini_parse("");
    ExpectStatus(media_cfg_from_ini(empty, &media), BASE_OK, "media default");
    ExpectInt(media.enc.fps, 25, "default fps");
    ExpectStatus(algo_cfg_from_ini(empty, &algo), BASE_OK, "algo default");
    ExpectStr(algo.cat_model, "models/Catto.rknn", "default cat");
    ExpectInt(algo.open.queue_depth, ALGO_QUEUE_DEFAULT, "default queue");
    ExpectStatus(app_cfg_from_ini(empty, &app), BASE_OK, "app default");
    ExpectInt(app.camera_count, 0, "no camera");
    ExpectStr(app.dir, "/opt/pet-monitor", "default dir");
    base_ini_free(empty);

    ini = base_ini_parse(
        "[media]\n"
        "fps = 10\n"
        "fps = 30\n"
        "codec = h265\n"
        "rc = vbr\n"
        "[algo]\n"
        "width = 640\n"
        "height = 480\n"
        "dev_id = 1\n"
        "queue_depth = 4\n"
        "score_min = 0.5\n"
        "[camera]\n"
        "timeout_ms = 5000\n"
        "[camera.bowl]\n"
        "url = rtsp://only\n"
        "place = EAT\n"
        "[camera.box]\n"
        "url = rtsp://box\n"
        "place = litter\n"
        "timeout_ms = 8000\n"
        "[email]\n"
        "enable = true\n"
        "password = \"a;b c\"\n"
        "from = a@b.c\n"
        "to = c@d.e\n");
    if (ini == NULL)
    {
        fprintf(stderr, "override text failed\n");
        g_failed = 1;
        return;
    }
    ExpectStatus(media_cfg_from_ini(ini, &media), BASE_OK, "override media");
    ExpectInt(media.enc.fps, 30, "last fps");
    ExpectInt(media.enc.codec, BASE_CODEC_H265, "h265");
    ExpectInt(media.enc.rc_mode, MEDIA_RC_VBR, "vbr");
    ExpectStatus(algo_cfg_from_ini(ini, &algo), BASE_OK, "override algo");
    ExpectInt(algo.open.width, 640, "override width");
    ExpectInt(algo.open.height, 480, "override height");
    ExpectInt(algo.open.dev_id, 1, "override dev");
    ExpectInt(algo.open.queue_depth, 4, "override queue");
    ExpectNear(algo.score_min, 0.5f, "override score");
    ExpectStatus(app_cfg_from_ini(ini, &app), BASE_OK, "override app");
    ExpectInt(app.camera_count, 2, "two cameras");
    ExpectStr(app.camera[0].name, "bowl", "bowl");
    ExpectInt(app.camera[0].place, APP_PLACE_EAT, "bowl place");
    ExpectInt(app.camera[0].timeout_ms, 5000, "inherited timeout");
    ExpectInt(app.camera[1].timeout_ms, 8000, "own timeout");
    ExpectStr(app.password, "a;b c", "mail password");
    ExpectInt(app.email_enable, 1, "mail on");
    if (app.email.password != app.password)
    {
        fprintf(stderr, "password pointer\n");
        g_failed = 1;
    }
    base_ini_free(ini);

    ini = base_ini_parse("[media]\nqp_min = 40\nqp_init = 10\nqp_max = 51\n");
    memset(media_fill, 0x5a, sizeof(media_fill));
    memcpy(&media, media_fill, sizeof(media));
    ExpectStatus(media_cfg_from_ini(ini, &media), BASE_ERR, "bad qp");
    ExpectUntouched(0x5a, (unsigned char *)&media, "bad qp out");
    base_ini_free(ini);

    ini = base_ini_parse("[email]\nenable = true\n");
    memset(&app, 0x5a, sizeof(app));
    ExpectStatus(app_cfg_from_ini(ini, &app), BASE_ERR, "mail without address");
    ExpectUntouched(0x5a, (unsigned char *)&app, "mail out");
    base_ini_free(ini);

    ini = base_ini_parse("[algo]\nqueue_depth = 9\n");
    memset(&algo, 0x5a, sizeof(algo));
    ExpectStatus(algo_cfg_from_ini(ini, &algo), BASE_ERR, "queue too deep");
    ExpectUntouched(0x5a, (unsigned char *)&algo, "queue out");
    base_ini_free(ini);

    {
        char text[2048];
        int used = 0;
        int i;
        used += snprintf(text + used, sizeof(text) - (size_t)used, "[camera]\ntimeout_ms = 1000\n");
        for (i = 0; i < APP_CAMERA_MAX + 1; i++)
        {
            used += snprintf(text + used, sizeof(text) - (size_t)used,
                             "[camera.c%d]\nurl = rtsp://127.0.0.1/%d\nplace = eat\n", i, i);
        }
        ini = base_ini_parse(text);
        memset(&app, 0x5a, sizeof(app));
        ExpectStatus(app_cfg_from_ini(ini, &app), BASE_ERR, "too many cameras");
        ExpectUntouched(0x5a, (unsigned char *)&app, "too many out");
        base_ini_free(ini);
    }
}

int main(void)
{
    TestParse();
    TestPath();
    TestLayers();
    return g_failed == 0 ? 0 : 1;
}
