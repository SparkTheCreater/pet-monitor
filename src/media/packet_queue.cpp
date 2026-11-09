/**
 * @file    packet_queue.cpp
 * @brief   包队列。P 帧依赖前面的帧，所以满了不能只丢一包。
 */
#include "packet_queue.h"

#include <new>

PmPacketQueue::PmPacketQueue(int limit)
    : limit_(limit)
    , dropping_(0)
    , dropped_(0)
    , stopped_(0)
{
}

bool PmPacketQueue::enqueue(const PmPacketView *packet)
{
    PmQueuedPacket item;
    item.data.assign(packet->data, packet->data + packet->size);
    item.key = packet->key ? 1 : 0;
    item.pts_us = packet->pts_us;
    item.recv_us = packet->recv_us;
    packets_.push_back(std::move(item));
    cond_.notify_one();
    return true;
}

bool PmPacketQueue::push(const PmPacketView *packet)
{
    if (packet == NULL || packet->data == NULL || packet->size == 0)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_)
    {
        return false;
    }
    if (dropping_)
    {
        if (!packet->key)
        {
            dropped_++;
            return true;
        }
        dropping_ = 0;
    }
    if ((int)packets_.size() >= limit_)
    {
        dropping_ = 1;
        dropped_++;
        return true;
    }
    try
    {
        return enqueue(packet);
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}

bool PmPacketQueue::try_pop(PmQueuedPacket *out)
{
    if (out == NULL)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (packets_.empty())
    {
        return false;
    }
    *out = std::move(packets_.front());
    packets_.pop_front();
    return true;
}

bool PmPacketQueue::pop(PmQueuedPacket *out)
{
    if (out == NULL)
    {
        return false;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    while (packets_.empty() && !stopped_)
    {
        cond_.wait(lock);
    }
    if (packets_.empty())
    {
        return false;
    }
    *out = std::move(packets_.front());
    packets_.pop_front();
    return true;
}

void PmPacketQueue::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = 1;
    cond_.notify_all();
}

void PmPacketQueue::drop_until_key()
{
    std::lock_guard<std::mutex> lock(mutex_);
    dropping_ = 1;
}

int PmPacketQueue::dropped() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}
