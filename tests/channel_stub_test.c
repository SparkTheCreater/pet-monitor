/**
 * @file    channel_stub_test.c
 * @brief   没有芯片时通道打不开，空指针调用也不崩。
 */
#include "media/pm_channel.h"

#include <stdio.h>
#include <string.h>

static int g_failed = 0;

static void ExpectErr(PmStatus status, const char *name)
{
    if (status == PM_ERR)
    {
        return;
    }
    fprintf(stderr, "%s returned %d\n", name, (int)status);
    g_failed = 1;
}

int main(void)
{
    PmChannelConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.channel = 0;
    cfg.codec = PM_CODEC_H264;
    cfg.buf_count = 8;
    cfg.queue_limit = 4;

    PmChannel *channel = (PmChannel *)1;
    ExpectErr(pm_channel_open(&cfg, &channel), "pm_channel_open");
    if (channel != NULL)
    {
        fprintf(stderr, "open left a channel\n");
        g_failed = 1;
    }
    pm_channel_close(NULL);
    ExpectErr(pm_channel_push(NULL, NULL), "pm_channel_push");
    if (pm_channel_dropped(NULL) != -1)
    {
        fprintf(stderr, "dropped on null\n");
        g_failed = 1;
    }
    if (pm_channel_subscribe(NULL, 0, NULL, NULL) != PM_ERR)
    {
        fprintf(stderr, "subscribe on null\n");
        g_failed = 1;
    }
    ExpectErr(pm_channel_unsubscribe(NULL, 0), "pm_channel_unsubscribe");
    ExpectErr(pm_channel_acquire(NULL, 0, NULL), "pm_channel_acquire");
    ExpectErr(pm_channel_release(NULL, 0), "pm_channel_release");
    ExpectErr(pm_channel_snapshot(NULL, NULL, NULL, 0, NULL, NULL), "pm_channel_snapshot");
    return g_failed ? 1 : 0;
}
