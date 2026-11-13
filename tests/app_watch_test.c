/**
 * @file    app_watch_test.c
 * @brief   用假时间把出现、留图、发信和消失复位走一遍。
 */
#include "app_watch.h"

#include <stdio.h>
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

static AppWatchEvent Step(AppWatch *watch, int64_t now, int has_cat)
{
    AppWatchParam param;
    AppWatchEvent ev;
    param.present_sec = 20;
    param.shot_sec = 10;
    param.absent_sec = 20;
    app_watch_step(watch, &param, now, has_cat, &ev);
    return ev;
}

static void TestCycle(void)
{
    AppWatch watch;
    AppWatchEvent ev;
    app_watch_reset(&watch);
    ev = Step(&watch, 0, 0);
    Expect(!ev.keep && !ev.send && watch.phase == APP_WATCH_IDLE, "idle empty");
    ev = Step(&watch, 0, 1);
    Expect(!ev.keep && !ev.send && watch.phase == APP_WATCH_SEEN, "appear");
    ev = Step(&watch, 9999, 1);
    Expect(!ev.keep && !ev.send && !watch.held, "before shot");
    ev = Step(&watch, 10000, 1);
    Expect(ev.keep && !ev.send, "shot");
    app_watch_kept(&watch);
    ev = Step(&watch, 19999, 1);
    Expect(!ev.keep && !ev.send && watch.phase == APP_WATCH_SEEN, "before send");
    ev = Step(&watch, 20000, 1);
    Expect(ev.send && !ev.keep && watch.phase == APP_WATCH_SENT, "send");
    ev = Step(&watch, 30000, 1);
    Expect(!ev.send && watch.phase == APP_WATCH_SENT, "latched");
    ev = Step(&watch, 30000, 0);
    Expect(watch.phase == APP_WATCH_SENT, "gone starts");
    ev = Step(&watch, 49999, 0);
    Expect(watch.phase == APP_WATCH_SENT, "absent short");
    ev = Step(&watch, 50000, 0);
    Expect(watch.phase == APP_WATCH_IDLE, "rearm");
    ev = Step(&watch, 50000, 1);
    Expect(watch.phase == APP_WATCH_SEEN && !ev.send, "second appear");
    ev = Step(&watch, 60000, 1);
    Expect(ev.keep, "second shot");
    app_watch_kept(&watch);
    ev = Step(&watch, 70000, 1);
    Expect(ev.send && watch.phase == APP_WATCH_SENT, "second send");
}

static void TestBreak(void)
{
    AppWatch watch;
    AppWatchEvent ev;
    app_watch_reset(&watch);
    Step(&watch, 0, 1);
    ev = Step(&watch, 10000, 1);
    Expect(ev.keep, "keep once");
    app_watch_kept(&watch);
    ev = Step(&watch, 15000, 0);
    Expect(ev.drop && !ev.send && watch.phase == APP_WATCH_IDLE, "drop");
    Step(&watch, 15000, 1);
    ev = Step(&watch, 25000, 1);
    Expect(ev.keep && !ev.send, "new shot");
    app_watch_kept(&watch);
    ev = Step(&watch, 26000, 0);
    Expect(ev.drop && watch.phase == APP_WATCH_IDLE, "drop again");
}

static void TestAbsentReset(void)
{
    AppWatch watch;
    AppWatchEvent ev;
    app_watch_reset(&watch);
    Step(&watch, 0, 1);
    Step(&watch, 10000, 1);
    app_watch_kept(&watch);
    ev = Step(&watch, 20000, 1);
    Expect(ev.send, "sent");
    Step(&watch, 20000, 0);
    Step(&watch, 30000, 0);
    ev = Step(&watch, 35000, 1);
    Expect(!ev.send && watch.phase == APP_WATCH_SENT, "cat returns");
    Step(&watch, 35000, 0);
    ev = Step(&watch, 54999, 0);
    Expect(watch.phase == APP_WATCH_SENT, "still latched");
    ev = Step(&watch, 55000, 0);
    Expect(watch.phase == APP_WATCH_IDLE, "absent done");
}

static void TestMissedShot(void)
{
    AppWatch watch;
    AppWatchEvent ev;
    app_watch_reset(&watch);
    Step(&watch, 0, 1);
    ev = Step(&watch, 20000, 1);
    Expect(!ev.send && !ev.keep && watch.phase == APP_WATCH_IDLE, "no picture no mail");
}

int main(void)
{
    TestCycle();
    TestBreak();
    TestAbsentReset();
    TestMissedShot();
    return g_failed == 0 ? 0 : 1;
}
