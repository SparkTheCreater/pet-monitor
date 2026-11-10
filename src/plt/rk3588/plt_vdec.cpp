/**
 * @file    plt_vdec.cpp
 * @brief   MPP 解码。外部缓冲组优先，失败再退回带上限的内部组。
 */
#include "plt/plt_vdec.h"

#include "plt_internal.h"

#include "mpp_buffer.h"
#include "mpp_frame.h"
#include "mpp_packet.h"
#include "rk_mpi.h"
#include "rk_vdec_cfg.h"

#include <stdio.h>
#include <string.h>

#include <new>
#include <vector>

struct Pool
{
    MppBufferGroup alloc_group;
    MppBufferGroup ext_group;
    MppBufferGroup half_group;
    std::vector<MppBuffer> bufs;
    size_t buf_size;

    Pool()
        : alloc_group(nullptr)
        , ext_group(nullptr)
        , half_group(nullptr)
        , buf_size(0)
    {
    }
};

struct PltVdec
{
    MppCtx ctx;
    MppApi *mpi;
    int buf_count;
    Pool pool;

    PltVdec()
        : ctx(nullptr)
        , mpi(nullptr)
        , buf_count(0)
    {
    }
};

namespace
{

void ClearPool(Pool *pool)
{
    for (size_t i = 0; i < pool->bufs.size(); ++i)
    {
        if (pool->bufs[i] != nullptr)
        {
            mpp_buffer_put(pool->bufs[i]);
            pool->bufs[i] = nullptr;
        }
    }
    pool->bufs.clear();
    if (pool->ext_group != nullptr)
    {
        mpp_buffer_group_put(pool->ext_group);
        pool->ext_group = nullptr;
    }
    if (pool->alloc_group != nullptr)
    {
        mpp_buffer_group_put(pool->alloc_group);
        pool->alloc_group = nullptr;
    }
    if (pool->half_group != nullptr)
    {
        mpp_buffer_group_put(pool->half_group);
        pool->half_group = nullptr;
    }
    pool->buf_size = 0;
}

int CommitExternal(PltVdec *dec, size_t buf_size)
{
    Pool *pool = &dec->pool;
    const int type = plt_rk_dma_type();
    const MppBufferType dma = (MppBufferType)type;
    if (type == 0 ||
        mpp_buffer_group_get_internal(&pool->alloc_group, dma) != MPP_OK ||
        mpp_buffer_group_get_external(&pool->ext_group, dma) != MPP_OK)
    {
        return BASE_ERR;
    }

    pool->bufs.assign((size_t)dec->buf_count, nullptr);
    for (int i = 0; i < dec->buf_count; ++i)
    {
        if (mpp_buffer_get(pool->alloc_group, &pool->bufs[(size_t)i], buf_size) != MPP_OK)
        {
            return BASE_ERR;
        }
        /*
         * 分配组里的缓冲必须一直留着，提交给解码器的只是同一块 dmabuf 的描述。
         * 提前释放分配组里的引用，这块 GEM 会被回收。
         */
        MppBufferInfo info;
        memset(&info, 0, sizeof(info));
        info.type = (MppBufferType)type;
        info.size = mpp_buffer_get_size(pool->bufs[(size_t)i]);
        info.ptr = mpp_buffer_get_ptr(pool->bufs[(size_t)i]);
        info.fd = mpp_buffer_get_fd(pool->bufs[(size_t)i]);
        info.index = i;
        if (info.fd < 0 || mpp_buffer_commit(pool->ext_group, &info) != MPP_OK)
        {
            return BASE_ERR;
        }
    }
    if (dec->mpi->control(dec->ctx, MPP_DEC_SET_EXT_BUF_GROUP, pool->ext_group) != MPP_OK)
    {
        return BASE_ERR;
    }
    pool->buf_size = buf_size;
    return BASE_OK;
}

int SetupHalf(PltVdec *dec, size_t buf_size)
{
    Pool *pool = &dec->pool;
    ClearPool(pool);
    const int type = plt_rk_dma_type();
    const MppBufferType dma = (MppBufferType)type;
    if (type == 0 || mpp_buffer_group_get_internal(&pool->half_group, dma) != MPP_OK)
    {
        return BASE_ERR;
    }
    if (mpp_buffer_group_limit_config(pool->half_group, buf_size, dec->buf_count) != MPP_OK)
    {
        return BASE_ERR;
    }
    if (dec->mpi->control(dec->ctx, MPP_DEC_SET_EXT_BUF_GROUP, pool->half_group) != MPP_OK)
    {
        return BASE_ERR;
    }
    pool->buf_size = buf_size;
    return BASE_OK;
}

int SetupPool(PltVdec *dec, MppFrame frame)
{
    const RK_U32 hor = mpp_frame_get_hor_stride(frame);
    const RK_U32 ver = mpp_frame_get_ver_stride(frame);
    size_t buf_size = mpp_frame_get_buf_size(frame);
    const size_t min_size = (size_t)hor * (size_t)ver * 2;
    if (buf_size < min_size)
    {
        buf_size = min_size;
    }
    /*
     * 尺寸没变就复用。这时调用方可能还拿着上一帧，拆掉分配组会把那块 fd 收走。
     */
    if (dec->pool.buf_size == buf_size &&
        (dec->pool.ext_group != nullptr || dec->pool.half_group != nullptr))
    {
        return BASE_OK;
    }

    Pool old;
    int ret = BASE_ERR;
    try
    {
        old.alloc_group = dec->pool.alloc_group;
        old.ext_group = dec->pool.ext_group;
        old.half_group = dec->pool.half_group;
        old.bufs.swap(dec->pool.bufs);
        old.buf_size = dec->pool.buf_size;
        dec->pool.alloc_group = nullptr;
        dec->pool.ext_group = nullptr;
        dec->pool.half_group = nullptr;
        dec->pool.buf_size = 0;

        ret = CommitExternal(dec, buf_size);
        if (ret != BASE_OK)
        {
            ClearPool(&dec->pool);
            ret = SetupHalf(dec, buf_size);
            if (ret == BASE_OK)
            {
                fprintf(stderr, "[plt.vdec] external pool failed, using limited internal group\n");
            }
        }
    }
    catch (const std::bad_alloc &)
    {
        ClearPool(&dec->pool);
        ClearPool(&old);
        return BASE_ERR;
    }
    ClearPool(&old);
    if (ret != BASE_OK)
    {
        fprintf(stderr, "[plt.vdec] setup buffer pool failed\n");
    }
    return ret;
}

int AcceptInfoChange(PltVdec *dec, MppFrame frame)
{
    const int ret = SetupPool(dec, frame);
    mpp_frame_deinit(&frame);
    if (ret != BASE_OK)
    {
        return BASE_ERR;
    }
    if (dec->mpi->control(dec->ctx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr) != MPP_OK)
    {
        fprintf(stderr, "[plt.vdec] info change ready failed\n");
        return BASE_ERR;
    }
    return BASE_OK;
}

void ClearFrame(BaseDecodedFrame *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->image.fd = -1;
}

}

extern "C" BaseStatus plt_vdec_open(int codec, int buf_count, PltVdec **out)
{
    if (out == nullptr || buf_count <= 0 || !plt_rk_ready())
    {
        if (out != nullptr)
        {
            *out = nullptr;
        }
        return BASE_ERR;
    }
    *out = nullptr;
    if (codec != BASE_CODEC_H264 && codec != BASE_CODEC_H265)
    {
        return BASE_ERR;
    }

    PltVdec *dec = new (std::nothrow) PltVdec();
    if (dec == nullptr)
    {
        return BASE_ERR;
    }
    dec->buf_count = buf_count;

    if (mpp_create(&dec->ctx, &dec->mpi) != MPP_OK)
    {
        delete dec;
        return BASE_ERR;
    }

    /* 这套 MPP 按 RK_S64 读超时。非阻塞，取不到帧就让调用方先去送下一包。 */
    RK_S64 timeout = MPP_POLL_NON_BLOCK;
    dec->mpi->control(dec->ctx, MPP_SET_INPUT_TIMEOUT, &timeout);
    dec->mpi->control(dec->ctx, MPP_SET_OUTPUT_TIMEOUT, &timeout);

    const MppCodingType coding = codec == BASE_CODEC_H265 ? MPP_VIDEO_CodingHEVC : MPP_VIDEO_CodingAVC;
    if (mpp_init(dec->ctx, MPP_CTX_DEC, coding) != MPP_OK)
    {
        mpp_destroy(dec->ctx);
        delete dec;
        return BASE_ERR;
    }

    MppDecCfg cfg = nullptr;
    if (mpp_dec_cfg_init(&cfg) == MPP_OK && cfg != nullptr)
    {
        dec->mpi->control(dec->ctx, MPP_DEC_GET_CFG, cfg);
        /* 调用方送来的已经是完整一帧，关掉解码器内部的分割。 */
        mpp_dec_cfg_set_u32(cfg, "base:split_parse", 0);
        dec->mpi->control(dec->ctx, MPP_DEC_SET_CFG, cfg);
        mpp_dec_cfg_deinit(cfg);
    }

    RK_U32 fmt = MPP_FMT_YUV420SP;
    dec->mpi->control(dec->ctx, MPP_DEC_SET_OUTPUT_FORMAT, &fmt);
    *out = dec;
    return BASE_OK;
}

extern "C" void plt_vdec_close(PltVdec *dec)
{
    if (dec == nullptr)
    {
        return;
    }
    /* 先停解码器，它手里的帧引用放掉，再拆缓冲池。 */
    if (dec->mpi != nullptr && dec->ctx != nullptr)
    {
        dec->mpi->reset(dec->ctx);
    }
    if (dec->ctx != nullptr)
    {
        mpp_destroy(dec->ctx);
        dec->ctx = nullptr;
    }
    ClearPool(&dec->pool);
    delete dec;
}

extern "C" BaseStatus plt_vdec_send(PltVdec *dec, const uint8_t *data, int len, int64_t pts_us, int eos)
{
    if (dec == nullptr || dec->mpi == nullptr)
    {
        return BASE_ERR;
    }
    if ((data == nullptr || len <= 0) && !eos)
    {
        return BASE_ERR;
    }

    MppPacket packet = nullptr;
    const size_t size = len > 0 ? (size_t)len : 0;
    if (mpp_packet_init(&packet, (void *)data, size) != MPP_OK)
    {
        return BASE_ERR;
    }
    mpp_packet_set_pts(packet, pts_us);
    if (eos)
    {
        mpp_packet_set_eos(packet);
    }

    const MPP_RET ret = dec->mpi->decode_put_packet(dec->ctx, packet);
    mpp_packet_deinit(&packet);
    if (ret == MPP_ERR_BUFFER_FULL)
    {
        return BASE_AGAIN;
    }
    if (ret != MPP_OK)
    {
        return BASE_ERR;
    }
    return BASE_OK;
}

extern "C" BaseStatus plt_vdec_get(PltVdec *dec, BaseDecodedFrame *frame)
{
    if (dec == nullptr || dec->mpi == nullptr || frame == nullptr)
    {
        return BASE_ERR;
    }
    ClearFrame(frame);

    /* 分辨率变化和坏帧会占掉一次取帧。最多再看几帧，把图像或结束空帧交出去。 */
    for (int i = 0; i < 8; ++i)
    {
        MppFrame raw = nullptr;
        const MPP_RET ret = dec->mpi->decode_get_frame(dec->ctx, &raw);
        if (ret != MPP_OK && ret != MPP_ERR_TIMEOUT)
        {
            return BASE_ERR;
        }
        if (raw == nullptr)
        {
            return BASE_AGAIN;
        }
        if (mpp_frame_get_info_change(raw))
        {
            if (AcceptInfoChange(dec, raw) != BASE_OK)
            {
                return BASE_ERR;
            }
            continue;
        }

        MppBuffer buffer = mpp_frame_get_buffer(raw);
        const int eos = mpp_frame_get_eos(raw) ? 1 : 0;
        if (buffer == nullptr || mpp_frame_get_errinfo(raw) || mpp_frame_get_discard(raw))
        {
            if (!eos)
            {
                mpp_frame_deinit(&raw);
                continue;
            }
            frame->eos = 1;
            frame->pts_us = mpp_frame_get_pts(raw);
            frame->owner = raw;
            return BASE_OK;
        }

        const MppFrameFormat fmt = mpp_frame_get_fmt(raw);
        if ((fmt & MPP_FRAME_FMT_MASK) != MPP_FMT_YUV420SP)
        {
            fprintf(stderr, "[plt.vdec] unsupported output format 0x%x\n", (unsigned)fmt);
            mpp_frame_deinit(&raw);
            ClearFrame(frame);
            return BASE_UNSUPPORTED;
        }

        const MppFrameColorRange range = mpp_frame_get_color_range(raw);
        frame->image.fd = mpp_buffer_get_fd(buffer);
        frame->image.ptr = mpp_buffer_get_ptr(buffer);
        frame->image.size = (int)mpp_buffer_get_size(buffer);
        frame->image.width = (int)mpp_frame_get_width(raw);
        frame->image.height = (int)mpp_frame_get_height(raw);
        frame->image.hor_stride = (int)mpp_frame_get_hor_stride(raw);
        frame->image.ver_stride = (int)mpp_frame_get_ver_stride(raw);
        frame->image.format = BASE_PIX_NV12;
        frame->image.range = range == MPP_FRAME_RANGE_JPEG ? BASE_RANGE_FULL : BASE_RANGE_LIMITED;
        frame->image.handle = buffer;
        frame->image.refcount = 1;
        frame->pts_us = mpp_frame_get_pts(raw);
        frame->eos = eos;
        frame->owner = raw;
        return BASE_OK;
    }
    return BASE_AGAIN;
}

extern "C" BaseStatus plt_vdec_release(PltVdec *dec, BaseDecodedFrame *frame)
{
    (void)dec;
    if (frame == nullptr)
    {
        return BASE_ERR;
    }
    if (frame->owner == nullptr)
    {
        return BASE_OK;
    }
    /*
     * deinit 只减这一帧的引用。缓冲仍在池里，解码器还会复用同一块 fd。
     * 图形和编码必须已经做完，否则这里归还后像素会被下一帧覆盖。
     */
    MppFrame raw = (MppFrame)frame->owner;
    mpp_frame_deinit(&raw);
    frame->owner = nullptr;
    frame->image.handle = nullptr;
    frame->image.fd = -1;
    frame->image.refcount = 0;
    return BASE_OK;
}
