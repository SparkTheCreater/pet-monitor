/**
 * @file    net_pull.h
 * @brief   拉一路 RTSP，吐出一帧 Annex B。
 *
 * 不解码，也不在断线后自己重连。net_read 在调用方线程上阻塞。
 * 交出的数据属于这个句柄，下一次 net_read 或 net_close 之前有效。
 * 解码器没收下这一帧时，调用方拿着它重送，不要先读下一帧。
 */
#ifndef NET_PULL_H
#define NET_PULL_H

#include "net_cfg.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct NetPull NetPull;

typedef struct NetPacket
{
    const uint8_t *data;
    int size;
    int64_t pts_us;
    int key;
    /* 直播流恒为 0。断开是 BASE_ERR，不是结束。 */
    int eos;
}
NetPacket;

typedef struct NetInfo
{
    /* BaseCodec。 */
    int codec;
    int width;
    int height;
    /* Annex B 或 H.264 AVCC。保持到 net_close。没有时指针为空、长度为 0。 */
    const uint8_t *extradata;
    int extradata_size;
}
NetInfo;

/*
 * 同步。transport 取 NetTransport。
 * 失败时 *out 为空。编码不是 H.264 或 H.265 时返回 BASE_UNSUPPORTED。
 */
BaseStatus net_open(const NetSource *source, int transport, NetPull **out);

/* 同步。先打断正在进行的 net_read，再释放。close(NULL) 直接返回。可从另一线程调用。 */
void net_close(NetPull *pull);

/* 打开成功后可取。参数集指针保持到 net_close。 */
BaseStatus net_info(const NetPull *pull, NetInfo *out);

/*
 * 阻塞，直到有一帧、连接断开，或被 net_close 打断。
 * 第一帧是关键帧。返回 BASE_OK 时 pkt 指向句柄内部的缓冲。
 */
BaseStatus net_read(NetPull *pull, NetPacket *pkt);

#ifdef __cplusplus
}
#endif

#endif
