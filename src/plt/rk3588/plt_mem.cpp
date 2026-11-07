/**
 * @file    plt_mem.cpp
 * @brief   从 DMA heap 申请 32 位可达的 dmabuf。RGA2 只能访问 32 位地址。
 */
#include "plt/plt_mem.h"

#include "plt_internal.h"

#include "mpp_buffer.h"

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#include <atomic>

namespace
{

struct DmaSync
{
    uint64_t flags;
};

#ifndef PM_DMA_BUF_IOCTL_SYNC
#define PM_DMA_BUF_BASE 'b'
#define PM_DMA_BUF_IOCTL_SYNC _IOW(PM_DMA_BUF_BASE, 0, DmaSync)
#endif

const uint64_t kSyncWrite = 2;
const uint64_t kSyncEnd = 1 << 2;

std::atomic<MppBufferGroup> g_group(nullptr);
std::atomic<int> g_type(0);

int Align16(int value)
{
    if (value <= 0)
    {
        return 0;
    }
    return (value + 15) & ~15;
}

int TryGroup(int type)
{
    MppBufferGroup group = nullptr;
    if (mpp_buffer_group_get_internal(&group, type) != MPP_OK || group == nullptr)
    {
        return PM_ERR;
    }
    g_type.store(type);
    g_group.store(group);
    return PM_OK;
}

void ClearImage(PmImage *image)
{
    if (image == nullptr)
    {
        return;
    }
    memset(image, 0, sizeof(*image));
    image->fd = -1;
}

}

int plt_rk_ready(void)
{
    return g_group.load() != nullptr ? 1 : 0;
}

int plt_rk_dma_type(void)
{
    return g_type.load();
}

int plt_rk_mem_open(void)
{
    const int dma32 = (int)MPP_BUFFER_TYPE_DMA_HEAP | (int)MPP_BUFFER_FLAGS_DMA32;
    if (TryGroup(dma32) == PM_OK)
    {
        return PM_OK;
    }
    fprintf(stderr, "[plt.mem] dma32 heap unavailable, trying dma heap\n");
    if (TryGroup((int)MPP_BUFFER_TYPE_DMA_HEAP) == PM_OK)
    {
        return PM_OK;
    }
    fprintf(stderr, "[plt.mem] dma heap unavailable, trying drm\n");
    if (TryGroup((int)MPP_BUFFER_TYPE_DRM) == PM_OK)
    {
        return PM_OK;
    }
    fprintf(stderr, "[plt.mem] no image allocator\n");
    return PM_ERR;
}

void plt_rk_mem_close(void)
{
    MppBufferGroup group = g_group.exchange(nullptr);
    g_type.store(0);
    if (group != nullptr)
    {
        mpp_buffer_group_put(group);
    }
}

extern "C" PmStatus plt_mem_alloc(int width, int height, int format, PmImage *out)
{
    if (out == nullptr || width <= 0 || height <= 0)
    {
        return PM_ERR;
    }
    if (format != PM_PIX_NV12 && format != PM_PIX_RGB888)
    {
        ClearImage(out);
        return PM_ERR;
    }
    MppBufferGroup group = g_group.load();
    if (group == nullptr)
    {
        ClearImage(out);
        fprintf(stderr, "[plt.mem] alloc before plt_init\n");
        return PM_ERR;
    }

    const int hor = Align16(width);
    const int ver = Align16(height);
    const size_t bytes = format == PM_PIX_NV12
                             ? (size_t)hor * (size_t)ver * 3 / 2
                             : (size_t)hor * (size_t)ver * 3;
    MppBuffer buffer = nullptr;
    if (mpp_buffer_get(group, &buffer, bytes) != MPP_OK || buffer == nullptr)
    {
        ClearImage(out);
        fprintf(stderr, "[plt.mem] get buffer failed\n");
        return PM_ERR;
    }

    memset(out, 0, sizeof(*out));
    out->fd = mpp_buffer_get_fd(buffer);
    out->ptr = mpp_buffer_get_ptr(buffer);
    out->size = (int)mpp_buffer_get_size(buffer);
    out->width = width;
    out->height = height;
    out->hor_stride = hor;
    out->ver_stride = ver;
    out->format = format;
    out->range = PM_RANGE_LIMITED;
    out->handle = buffer;
    out->refcount = 1;
    if (out->fd < 0 || out->ptr == nullptr || out->size <= 0)
    {
        mpp_buffer_put(buffer);
        ClearImage(out);
        return PM_ERR;
    }
    return PM_OK;
}

extern "C" void plt_mem_free(PmImage *image)
{
    if (!pm_image_valid(image))
    {
        ClearImage(image);
        return;
    }
    mpp_buffer_put((MppBuffer)image->handle);
    ClearImage(image);
}

extern "C" PmStatus plt_mem_flush(PmImage *image)
{
    if (!pm_image_valid(image) || image->fd < 0)
    {
        return PM_ERR;
    }
    DmaSync sync;
    sync.flags = kSyncWrite | kSyncEnd;
    if (ioctl(image->fd, PM_DMA_BUF_IOCTL_SYNC, &sync) != 0)
    {
        return PM_ERR;
    }
    return PM_OK;
}
