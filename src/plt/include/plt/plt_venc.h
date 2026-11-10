/**
 * @file    plt_venc.h
 * @brief   视频编码。输入是 NV12 或 RGB888，格式在 open 时定下。
 */
#ifndef PLT_VENC_H
#define PLT_VENC_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct PltVenc PltVenc;

typedef enum
{
    PLT_RC_CBR = 0,
    PLT_RC_VBR = 1
}
PltRateControl;

/* 每个字段都要合法。不合法时 open 失败，不会改用另一套参数。 */
typedef struct PltVencParam
{
    int codec;
    int format;
    int width;
    int height;
    int fps;
    int gop;
    int rc_mode;
    int bitrate;
    int qp_init;
    int qp_min;
    int qp_max;
    /* 只对 H.264：66 Baseline、77 Main、100 High。level 40 即 4.0。 */
    int profile;
    int level;
}
PltVencParam;

/*
 * 取出来的一包。data 指向编码器内部缓存，plt_venc_get_done 或 close 之后失效。
 * 调用方要在失效前把字节拷走。
 */
typedef struct PltVencPacket
{
    const uint8_t *data;
    size_t size;
    int64_t pts_us;
    int key;
}
PltVencPacket;

typedef struct PltVencRoi
{
    int x;
    int y;
    int w;
    int h;
    int abs_qp;
    int qp;
    int intra;
}
PltVencRoi;

/*
 * 同步。参数不合法，或 plt_init 还没成功，返回 BASE_ERR，*out 为空。
 * 这颗平台没有视频编码器时返回 BASE_UNSUPPORTED。
 */
BaseStatus plt_venc_open(const PltVencParam *param, PltVenc **out);

/* 停掉编码器并释放内部码流。之后 get 拿到的指针都失效。 */
void plt_venc_close(PltVenc *enc);

/*
 * 同步。格式、宽高和行宽要和 open 时一致。
 * 返回时这一帧的码流已经取进内部队列。image 在返回前不能被改写。
 */
BaseStatus plt_venc_send(PltVenc *enc, const BaseImage *image, int64_t pts_us);

/*
 * 非阻塞。没有包返回 BASE_AGAIN。
 * 同一包会一直留到 plt_venc_get_done。下一次 get 才能拿到后面的包。
 */
BaseStatus plt_venc_get(PltVenc *enc, PltVencPacket *packet);

/* 上一包已经拷走。空指针直接返回。 */
void plt_venc_get_done(PltVenc *enc);

/* 下一帧编成关键帧并带上参数集。和 send 由调用方串行调用。 */
BaseStatus plt_venc_request_idr(PltVenc *enc);

/* 下一帧起用新的目标码率。码率模式和 QP 不变。 */
BaseStatus plt_venc_set_bitrate(PltVenc *enc, int bitrate);

/*
 * 设置下一帧起的感兴趣区域。最多 8 块，count 为 0 时清掉。
 * 坐标和宽高按像素，必须 16 对齐，并且落在画面里。
 * 返回前已经把参数拷走。不合法时保持原配置。
 */
BaseStatus plt_venc_set_roi(PltVenc *enc, const PltVencRoi *regions, int count);

#ifdef __cplusplus
}
#endif

#endif
