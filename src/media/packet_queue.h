/**
 * @file    packet_queue.h
 * @brief   有上限的码流队列。满了丢到下一个关键帧。
 */
#ifndef PM_PACKET_QUEUE_H
#define PM_PACKET_QUEUE_H

#include "pm_packet.h"

#include <stdint.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

struct PmQueuedPacket
{
    std::vector<uint8_t> data;
    int key;
    int64_t pts_us;
    int64_t recv_us;
};

class PmPacketQueue
{
public:
    explicit PmPacketQueue(int limit);

    /* 拷走这一包。队列满时丢包并返回 true。已经停止时返回 false。 */
    bool push(const PmPacketView *packet);
    bool try_pop(PmQueuedPacket *out);
    /* 等到有包或被停止。停止且队列空时返回 false。 */
    bool pop(PmQueuedPacket *out);
    void stop();
    /* 当前包也不再送，后面的非关键帧一起丢掉。 */
    void drop_until_key();
    int dropped() const;

private:
    bool enqueue(const PmPacketView *packet);

    int limit_;
    int dropping_;
    int dropped_;
    int stopped_;
    std::deque<PmQueuedPacket> packets_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
};

#endif
