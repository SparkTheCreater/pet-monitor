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

static AppWatchEvent StepBoth(AppWatch *watch, int64_t now, int frame_cats, int roi_cats)
{
    AppWatchParam param;
    AppWatchEvent ev;
    param.present_sec = 20;
    param.shot_sec = 10;
    param.pair_sec = 3;
    param.cooldown_sec = 20;
    app_watch_step(watch, &param, now, frame_cats, roi_cats, &ev);
    return ev;
}

static AppWatchEvent Step(AppWatch *watch, int64_t now, int cats)
{
    return StepBoth(watch, now, cats, cats);
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
    Expect(!ev.send && watch.phase == APP_WATCH_SENT, "cooling");
    ev = Step(&watch, 30000, 0);
    Expect(watch.phase == APP_WATCH_SENT, "cool ignores absence");
    ev = Step(&watch, 39999, 2);
    Expect(!ev.send && watch.phase == APP_WATCH_SENT, "cool ignores return");
    ev = Step(&watch, 40000, 1);
    Expect(!ev.send && watch.phase == APP_WATCH_SEEN, "recount");
    ev = Step(&watch, 50000, 1);
    Expect(ev.keep && !ev.send, "second shot");
    app_watch_kept(&watch);
    ev = Step(&watch, 60000, 1);
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

static void TestCool(void)
{
    AppWatch watch;
    AppWatchEvent ev;
    app_watch_reset(&watch);
    Step(&watch, 0, 1);
    Step(&watch, 10000, 1);
    app_watch_kept(&watch);
    ev = Step(&watch, 20000, 1);
    Expect(ev.send, "sent");
    ev = Step(&watch, 20000, 0);
    Expect(!ev.send && watch.phase == APP_WATCH_SENT, "just sent");
    ev = Step(&watch, 30000, 2);
    Expect(!ev.send && watch.phase == APP_WATCH_SENT, "pair during cool");
    ev = Step(&watch, 39999, 0);
    Expect(watch.phase == APP_WATCH_SENT, "cool holds");
    ev = Step(&watch, 40000, 2);
    Expect(!ev.send && watch.phase == APP_WATCH_SEEN, "cool done recounts");
    ev = Step(&watch, 43000, 2);
    Expect(ev.keep && ev.send, "pair after cool");
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

static AppVerdict JudgeAt(AppWatchJudge *judge, int64_t now, int has_cat)
{
    AppFact fact;
    AppVerdict verdict;
    memset(&fact, 0, sizeof(fact));
    fact.now_ms = now;
    fact.has_cat = has_cat > 0;
    fact.cats = has_cat;
    fact.roi_cats = has_cat;
    app_watch_judge(&fact, judge, &verdict);
    return verdict;
}

static void TestJudge(void)
{
    AppWatch watch;
    AppWatchJudge judge;
    AppWatchEvent ev;
    AppVerdict verdict;
    int64_t now[8] = {0, 0, 10000, 15000, 15000, 25000, 35000, 45000};
    int cat[8] = {0, 1, 1, 0, 1, 1, 1, 1};
    int i;
    memset(&judge, 0, sizeof(judge));
    judge.param.present_sec = 20;
    judge.param.shot_sec = 10;
    judge.param.pair_sec = 3;
    judge.param.cooldown_sec = 20;
    app_watch_reset(&watch);
    app_watch_judge_reset(&judge);
    for (i = 0; i < 8; i++)
    {
        ev = Step(&watch, now[i], cat[i]);
        verdict = JudgeAt(&judge, now[i], cat[i]);
        Expect(verdict.keep == ev.keep && verdict.send == ev.send && verdict.drop == ev.drop, "judge matches");
        if (ev.keep)
        {
            app_watch_kept(&watch);
            app_watch_judge_kept(&judge);
        }
    }
    Expect(judge.watch.phase == watch.phase && judge.watch.held == watch.held, "judge state");
}

static void TestPair(void)
{
    AppWatch watch;
    AppWatchJudge judge;
    AppWatchEvent ev;
    AppVerdict verdict;
    app_watch_reset(&watch);
    ev = Step(&watch, 0, 2);
    Expect(!ev.keep && !ev.send && watch.phase == APP_WATCH_SEEN, "pair starts");
    ev = Step(&watch, 2999, 2);
    Expect(!ev.send, "pair short");
    ev = Step(&watch, 3000, 2);
    Expect(ev.keep && ev.send && ev.pair && watch.phase == APP_WATCH_SEEN, "pair ready");
    app_watch_kept(&watch);
    Expect(watch.phase == APP_WATCH_SENT, "pair sent");
    ev = Step(&watch, 4000, 2);
    Expect(!ev.send && watch.phase == APP_WATCH_SENT, "pair cooling");
    memset(&judge, 0, sizeof(judge));
    judge.param.present_sec = 20;
    judge.param.shot_sec = 10;
    judge.param.pair_sec = 3;
    judge.param.cooldown_sec = 20;
    app_watch_judge_reset(&judge);
    verdict = JudgeAt(&judge, 0, 2);
    Expect(!verdict.keep && !verdict.send, "judge pair waits");
    verdict = JudgeAt(&judge, 3000, 2);
    Expect(verdict.keep && verdict.send && verdict.pair, "judge pair ready");
    app_watch_judge_kept(&judge);
    Expect(judge.watch.phase == APP_WATCH_SENT, "judge pair sent");
}

static void TestPairBreak(void)
{
    AppWatch watch;
    AppWatchEvent ev;
    app_watch_reset(&watch);
    Step(&watch, 0, 1);
    ev = Step(&watch, 2000, 2);
    Expect(!ev.send, "pair arm");
    ev = Step(&watch, 4000, 1);
    Expect(!ev.send && watch.phase == APP_WATCH_SEEN, "pair dropped");
    ev = Step(&watch, 10000, 1);
    Expect(ev.keep && !ev.send, "one cat still timing");
    app_watch_kept(&watch);
    ev = Step(&watch, 20000, 1);
    Expect(ev.send && !ev.keep, "stay after pair break");
}

static void TestPairOrStay(void)
{
    AppWatch watch;
    AppWatchEvent ev;
    app_watch_reset(&watch);
    Step(&watch, 0, 1);
    ev = Step(&watch, 5000, 2);
    Expect(!ev.send, "pair not yet");
    ev = Step(&watch, 8000, 2);
    Expect(ev.keep && ev.send && watch.phase == APP_WATCH_SEEN, "pair before stay");
    app_watch_kept(&watch);
    Expect(watch.phase == APP_WATCH_SENT, "pair seals");
    app_watch_reset(&watch);
    Step(&watch, 0, 1);
    ev = Step(&watch, 10000, 1);
    Expect(ev.keep && !ev.send, "one cat waits");
    app_watch_kept(&watch);
    ev = Step(&watch, 20000, 1);
    Expect(ev.send && !ev.keep && !ev.pair && watch.phase == APP_WATCH_SENT, "stay alone");
}

static void TestSplit(void)
{
    AppWatch watch;
    AppWatchEvent ev;
    app_watch_reset(&watch);
    ev = StepBoth(&watch, 0, 1, 0);
    Expect(!ev.keep && !ev.send && watch.phase == APP_WATCH_IDLE, "outside one");
    ev = StepBoth(&watch, 20000, 1, 0);
    Expect(!ev.keep && !ev.send && watch.phase == APP_WATCH_IDLE, "outside stays idle");
    app_watch_reset(&watch);
    ev = StepBoth(&watch, 0, 2, 0);
    Expect(!ev.send && watch.phase == APP_WATCH_SEEN, "outside pair");
    ev = StepBoth(&watch, 3000, 2, 0);
    Expect(ev.keep && ev.send && ev.pair, "outside pair ready");
    app_watch_reset(&watch);
    StepBoth(&watch, 0, 2, 1);
    ev = StepBoth(&watch, 2000, 1, 1);
    Expect(!ev.send && watch.phase == APP_WATCH_SEEN, "pair break keeps roi");
    ev = StepBoth(&watch, 10000, 1, 1);
    Expect(ev.keep && !ev.send && !ev.pair, "roi shot");
    app_watch_kept(&watch);
    ev = StepBoth(&watch, 20000, 1, 1);
    Expect(ev.send && !ev.pair, "roi send");
    app_watch_reset(&watch);
    StepBoth(&watch, 0, 1, 1);
    ev = StepBoth(&watch, 10000, 1, 1);
    Expect(ev.keep, "hold roi");
    app_watch_kept(&watch);
    ev = StepBoth(&watch, 12000, 2, 0);
    Expect(ev.drop && !ev.send && watch.phase == APP_WATCH_SEEN, "roi gone pair stays");
    ev = StepBoth(&watch, 15000, 2, 0);
    Expect(ev.keep && ev.send && ev.pair, "pair after roi drop");
}

int main(void)
{
    TestCycle();
    TestBreak();
    TestCool();
    TestMissedShot();
    TestJudge();
    TestPair();
    TestPairBreak();
    TestPairOrStay();
    TestSplit();
    return g_failed == 0 ? 0 : 1;
}
