/**
 * @file    app_act_test.c
 * @brief   执行是一个函数。默认函数只做为真的那一步。
 */
#include "app_act.h"
#include "app_cfg.h"
#include "media_jpeg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

static void Expect(int cond, const char *name)
{
    if (cond)
    {
        return;
    }
    fprintf(stderr, "%s\n", name);
    g_failed = 1;
}

typedef struct Trace
{
    int kept;
    int sent;
}
Trace;

static void OnKept(void *user)
{
    ((Trace *)user)->kept += 1;
}

static void Run(const AppVerdict *want, Trace *trace, uint8_t **jpeg, size_t *jpeg_len)
{
    AppShot shot;
    AppFact fact;
    AppCamera camera;
    memset(&shot, 0, sizeof(shot));
    memset(&fact, 0, sizeof(fact));
    memset(&camera, 0, sizeof(camera));
    snprintf(camera.name, sizeof(camera.name), "bowl");
    fact.camera = &camera;
    shot.fact = &fact;
    shot.jpeg = jpeg;
    shot.jpeg_len = jpeg_len;
    shot.kept = OnKept;
    shot.kept_user = trace;
    app_act_run(&shot, want, NULL);
}

static uint8_t *Hold(size_t *len)
{
    uint8_t *jpeg = malloc(4);
    if (jpeg == NULL)
    {
        return NULL;
    }
    jpeg[0] = 1;
    *len = 4;
    return jpeg;
}

static void TestFalse(void)
{
    AppVerdict want;
    Trace trace;
    uint8_t *jpeg;
    size_t len;
    uint8_t *kept;
    memset(&want, 0, sizeof(want));
    memset(&trace, 0, sizeof(trace));
    jpeg = Hold(&len);
    kept = jpeg;
    Run(&want, &trace, &jpeg, &len);
    Expect(trace.kept == 0 && jpeg == kept, "false skips");
    free(jpeg);
}

static void TestDrop(void)
{
    AppVerdict want;
    Trace trace;
    uint8_t *jpeg;
    size_t len;
    memset(&want, 0, sizeof(want));
    memset(&trace, 0, sizeof(trace));
    want.drop = 1;
    jpeg = Hold(&len);
    Run(&want, &trace, &jpeg, &len);
    Expect(trace.kept == 0 && jpeg == NULL && len == 0, "drop only");
}

static void TestSendNeedsMail(void)
{
    AppVerdict want;
    Trace trace;
    uint8_t *jpeg;
    size_t len;
    memset(&want, 0, sizeof(want));
    memset(&trace, 0, sizeof(trace));
    want.send = 1;
    jpeg = Hold(&len);
    Run(&want, &trace, &jpeg, &len);
    Expect(jpeg != NULL && len == 4, "send without mail");
    free(jpeg);
}

int main(void)
{
    TestFalse();
    TestDrop();
    TestSendNeedsMail();
    return g_failed == 0 ? 0 : 1;
}
