/**
 * @file    app_act_test.c
 * @brief   执行是一个函数。默认函数只做为真的那一步。
 */
#define _POSIX_C_SOURCE 200809L

#include "app_act.h"
#include "app_cfg.h"
#include "media_jpeg.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

static int SaveOnce(const char *dir, int pair, const char *tail)
{
    AppShot shot;
    AppFact fact;
    AppVerdict verdict;
    AppCamera camera;
    uint8_t *jpeg;
    size_t len;
    DIR *folder;
    struct dirent *ent;
    int found;
    jpeg = Hold(&len);
    memset(&shot, 0, sizeof(shot));
    memset(&fact, 0, sizeof(fact));
    memset(&verdict, 0, sizeof(verdict));
    memset(&camera, 0, sizeof(camera));
    snprintf(camera.name, sizeof(camera.name), "客厅");
    fact.camera = &camera;
    verdict.pair = pair;
    verdict.send = 1;
    shot.fact = &fact;
    shot.jpeg = &jpeg;
    shot.jpeg_len = &len;
    shot.dir = dir;
    app_act_run(&shot, &verdict, NULL);
    Expect(jpeg != NULL && len == 4, "save keeps buffer");
    folder = opendir(dir);
    found = 0;
    if (folder != NULL)
    {
        while ((ent = readdir(folder)) != NULL)
        {
            char path[256];
            FILE *fp;
            uint8_t buf[4];
            if (strstr(ent->d_name, tail) == NULL)
            {
                continue;
            }
            snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
            fp = fopen(path, "rb");
            if (fp != NULL && fread(buf, 1, 4, fp) == 4 && buf[0] == 1)
            {
                found = 1;
            }
            if (fp != NULL)
            {
                fclose(fp);
            }
            remove(path);
        }
        closedir(folder);
    }
    free(jpeg);
    return found;
}

static void TestSave(void)
{
    char dir[] = "/tmp/pm-shots-XXXXXX";
    if (mkdtemp(dir) == NULL)
    {
        fprintf(stderr, "mkdtemp\n");
        g_failed = 1;
        return;
    }
    Expect(SaveOnce(dir, 0, "_event_") == 1, "one cat name");
    Expect(SaveOnce(dir, 1, "_2cats_") == 1, "two cat name");
    rmdir(dir);
}

int main(void)
{
    TestFalse();
    TestDrop();
    TestSendNeedsMail();
    TestSave();
    return g_failed == 0 ? 0 : 1;
}
