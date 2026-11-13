/**
 * @file    net_test.c
 * @brief   相机段解析，以及打不开的地址不会留下句柄。
 */
#include "net_cfg.h"
#include "net_pull.h"

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

static void ExpectUntouched(unsigned char fill, const unsigned char *p, const char *name)
{
    if (*p == fill)
    {
        return;
    }
    fprintf(stderr, "%s was written\n", name);
    g_failed = 1;
}

static void ExpectFail(const char *text, const char *name)
{
    BaseIni *ini = base_ini_parse(text);
    NetConfig cfg;
    if (ini == NULL)
    {
        fprintf(stderr, "%s text failed\n", name);
        g_failed = 1;
        return;
    }
    memset(&cfg, 0x5a, sizeof(cfg));
    ExpectStatus(net_cfg_from_ini(ini, &cfg), BASE_ERR, name);
    ExpectUntouched(0x5a, (unsigned char *)&cfg, name);
    base_ini_free(ini);
}

static void TestSample(void)
{
    BaseIni *ini = base_ini_load(PM_INI_SAMPLE);
    NetConfig cfg;
    if (ini == NULL)
    {
        fprintf(stderr, "sample missing\n");
        g_failed = 1;
        return;
    }
    ExpectStatus(net_cfg_from_ini(ini, &cfg), BASE_OK, "sample");
    ExpectInt(cfg.source_count, 2, "count");
    ExpectInt(cfg.transport, NET_TRANSPORT_TCP, "transport");
    ExpectStr(cfg.source[0].name, "food", "food name");
    ExpectStr(cfg.source[0].url, "rtsp://192.168.1.20:554/stream1", "food url");
    ExpectInt(cfg.source[0].timeout_ms, 5000, "food timeout");
    ExpectStr(cfg.source[1].name, "litter", "litter name");
    ExpectStr(cfg.source[1].url, "rtsp://192.168.1.21:554/stream1", "litter url");
    ExpectInt(cfg.source[1].timeout_ms, 5000, "litter timeout");
    base_ini_free(ini);
}

static void TestOverride(void)
{
    BaseIni *ini = base_ini_parse(
        "[camera]\n"
        "timeout_ms = 4000\n"
        "transport = UDP\n"
        "[camera.bowl]\n"
        "url = RTSP://cam/live\n"
        "[camera.box]\n"
        "url = rtsp://cam/sub\n"
        "timeout_ms = 1500\n");
    NetConfig cfg;
    if (ini == NULL)
    {
        fprintf(stderr, "override text failed\n");
        g_failed = 1;
        return;
    }
    ExpectStatus(net_cfg_from_ini(ini, &cfg), BASE_OK, "override");
    ExpectInt(cfg.transport, NET_TRANSPORT_UDP, "udp");
    ExpectInt(cfg.source_count, 2, "two");
    ExpectStr(cfg.source[0].name, "bowl", "bowl");
    ExpectStr(cfg.source[0].url, "RTSP://cam/live", "scheme");
    ExpectInt(cfg.source[0].timeout_ms, 4000, "shared timeout");
    ExpectInt(cfg.source[1].timeout_ms, 1500, "own timeout");
    base_ini_free(ini);
}

static void TestReject(void)
{
    char text[2048];
    int used = 0;
    int i;
    ExpectStatus(net_cfg_from_ini(NULL, NULL), BASE_ERR, "null");
    ExpectFail("[camera.food]\nplace = eat\n", "missing url");
    ExpectFail("[camera.food]\nurl =\n", "empty url");
    ExpectFail("[camera.food]\nurl = http://cam/live\n", "not rtsp");
    ExpectFail("[camera]\ntimeout_ms = 0\n[camera.food]\nurl = rtsp://cam/live\n", "zero timeout");
    ExpectFail("[camera.food]\nurl = rtsp://cam/live\ntimeout_ms = -1\n", "negative timeout");
    ExpectFail("[camera]\ntransport = quic\n[camera.food]\nurl = rtsp://cam/live\n", "bad transport");
    ExpectFail("[camera.a.b]\nurl = rtsp://cam/live\n", "dotted name");
    used += snprintf(text + used, sizeof(text) - (size_t)used, "[camera]\ntimeout_ms = 1000\n");
    for (i = 0; i < NET_SOURCE_MAX + 1; i++)
    {
        used += snprintf(text + used, sizeof(text) - (size_t)used,
                         "[camera.c%d]\nurl = rtsp://127.0.0.1/%d\n", i, i);
    }
    ExpectFail(text, "too many");
}

static void TestOpen(void)
{
    NetSource source;
    NetPull *pull = (NetPull *)1;
    NetInfo info;
    memset(&source, 0, sizeof(source));
    snprintf(source.url, sizeof(source.url), "rtsp://127.0.0.1:9/none");
    source.timeout_ms = 500;
    ExpectStatus(net_open(NULL, NET_TRANSPORT_TCP, &pull), BASE_ERR, "null source");
    if (pull != NULL)
    {
        fprintf(stderr, "null source left a handle\n");
        g_failed = 1;
        pull = NULL;
    }
    pull = (NetPull *)1;
    ExpectStatus(net_open(&source, 9, &pull), BASE_ERR, "bad transport");
    if (pull != NULL)
    {
        fprintf(stderr, "bad transport left a handle\n");
        g_failed = 1;
        net_close(pull);
        pull = NULL;
    }
    pull = (NetPull *)1;
    ExpectStatus(net_open(&source, NET_TRANSPORT_TCP, &pull), BASE_ERR, "refused");
    if (pull != NULL)
    {
        fprintf(stderr, "refused left a handle\n");
        g_failed = 1;
        net_close(pull);
    }
    ExpectStatus(net_open(&source, NET_TRANSPORT_TCP, NULL), BASE_ERR, "null out");
    ExpectStatus(net_info(NULL, &info), BASE_ERR, "null info");
    ExpectStatus(net_read(NULL, NULL), BASE_ERR, "null read");
    net_close(NULL);
}

int main(void)
{
    TestSample();
    TestOverride();
    TestReject();
    TestOpen();
    return g_failed == 0 ? 0 : 1;
}
