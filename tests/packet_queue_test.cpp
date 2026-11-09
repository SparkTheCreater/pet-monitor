/**
 * @file    packet_queue_test.cpp
 * @brief   队列满了要丢到下一个关键帧，不能只丢掉当前这一包。
 */
#include "packet_queue.h"

#include <stdio.h>
#include <string.h>

static int g_failed = 0;

static void Expect(int cond, const char *name)
{
    if (cond)
    {
        return;
    }
    fprintf(stderr, "fail %s\n", name);
    g_failed = 1;
}

static PmPacketView Make(const char *text, int key, int64_t pts)
{
    PmPacketView packet;
    memset(&packet, 0, sizeof(packet));
    packet.key = key;
    packet.pts_us = pts;
    packet.recv_us = pts;
    packet.data = (const uint8_t *)text;
    packet.size = strlen(text);
    return packet;
}

int main(void)
{
    PmPacketQueue queue(2);
    PmPacketView a = Make("A", 1, 1);
    PmPacketView b = Make("B", 1, 2);
    PmPacketView c = Make("C", 0, 3);
    PmPacketView d = Make("D", 0, 4);
    PmPacketView e = Make("E", 1, 5);
    Expect(queue.push(&a), "push A");
    Expect(queue.push(&b), "push B");
    Expect(queue.push(&c), "drop C");
    Expect(queue.push(&d), "drop D");
    Expect(queue.dropped() == 2, "dropped two");

    PmQueuedPacket out;
    Expect(queue.try_pop(&out) && out.data.size() == 1 && out.data[0] == 'A', "pop A");
    Expect(queue.push(&e), "push E");
    Expect(queue.dropped() == 2, "key after space is kept");
    Expect(queue.try_pop(&out) && out.data[0] == 'B', "pop B");
    Expect(queue.try_pop(&out) && out.data[0] == 'E' && out.key == 1, "pop E");
    Expect(!queue.try_pop(&out), "empty");

    queue.stop();
    PmPacketView f = Make("F", 1, 6);
    Expect(!queue.push(&f), "stopped");
    return g_failed ? 1 : 0;
}
