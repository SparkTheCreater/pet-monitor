/**
 * @file    plt_rga.cpp
 * @brief   用 RGA 做裁剪、缩放、转色和描边。返回时硬件已经完成。
 */
#include "plt/plt_rga.h"

#include "im2d.h"

#include <stdio.h>
#include <string.h>

namespace
{

struct Span
{
    int x;
    int y;
    int w;
    int h;
};

struct RgaImage
{
    rga_buffer_handle_t handle;
    rga_buffer_t buf;

    RgaImage()
        : handle(0)
    {
        memset(&buf, 0, sizeof(buf));
    }

    ~RgaImage()
    {
        if (handle != 0)
        {
            releasebuffer_handle(handle);
        }
    }
};

int IsYuv(int format)
{
    return format == PM_PIX_NV12;
}

int RgaFormat(int format)
{
    if (format == PM_PIX_NV12)
    {
        return RK_FORMAT_YCbCr_420_SP;
    }
    if (format == PM_PIX_RGB888)
    {
        return RK_FORMAT_RGB_888;
    }
    return -1;
}

int RgaOk(IM_STATUS status)
{
    return (int)status > 0;
}

Span Whole(const PmImage *image)
{
    Span span;
    span.x = 0;
    span.y = 0;
    span.w = image->width;
    span.h = image->height;
    return span;
}

Span FromRect(const PmImage *image, const PmRect *rect)
{
    if (rect == nullptr)
    {
        return Whole(image);
    }
    Span span;
    span.x = rect->x;
    span.y = rect->y;
    span.w = rect->w;
    span.h = rect->h;
    return span;
}

void ClearSpan(Span *span)
{
    span->x = 0;
    span->y = 0;
    span->w = 0;
    span->h = 0;
}

int ClipMapped(Span src, int src_w, int src_h, Span dst, int dst_w, int dst_h, Span *src_out, Span *dst_out)
{
    if (src.w < 0 || src.h < 0 || dst.w < 0 || dst.h < 0)
    {
        return PM_ERR;
    }
    if (src.w == 0 || src.h == 0 || dst.w == 0 || dst.h == 0)
    {
        ClearSpan(src_out);
        ClearSpan(dst_out);
        return PM_OK;
    }

    const int dx0 = dst.x;
    const int dy0 = dst.y;
    const int dx1 = dst.x + dst.w;
    const int dy1 = dst.y + dst.h;
    int cx0 = dx0 < 0 ? 0 : dx0;
    int cy0 = dy0 < 0 ? 0 : dy0;
    int cx1 = dx1 > dst_w ? dst_w : dx1;
    int cy1 = dy1 > dst_h ? dst_h : dy1;
    if (cx1 <= cx0 || cy1 <= cy0)
    {
        ClearSpan(src_out);
        ClearSpan(dst_out);
        return PM_OK;
    }

    int sx0 = src.x + (int)(((int64_t)(cx0 - dx0) * src.w) / dst.w);
    int sy0 = src.y + (int)(((int64_t)(cy0 - dy0) * src.h) / dst.h);
    int sx1 = src.x + (int)(((int64_t)(cx1 - dx0) * src.w) / dst.w);
    int sy1 = src.y + (int)(((int64_t)(cy1 - dy0) * src.h) / dst.h);
    if (sx1 < sx0)
    {
        sx1 = sx0;
    }
    if (sy1 < sy0)
    {
        sy1 = sy0;
    }

    int qx0 = sx0 < 0 ? 0 : sx0;
    int qy0 = sy0 < 0 ? 0 : sy0;
    int qx1 = sx1 > src_w ? src_w : sx1;
    int qy1 = sy1 > src_h ? src_h : sy1;
    if (qx1 <= qx0 || qy1 <= qy0 || sx1 <= sx0 || sy1 <= sy0)
    {
        ClearSpan(src_out);
        ClearSpan(dst_out);
        return PM_OK;
    }

    const int64_t sw = sx1 - sx0;
    const int64_t sh = sy1 - sy0;
    const int cw = cx1 - cx0;
    const int ch = cy1 - cy0;
    const int nx0 = cx0 + (int)(((int64_t)(qx0 - sx0) * cw) / sw);
    const int ny0 = cy0 + (int)(((int64_t)(qy0 - sy0) * ch) / sh);
    const int nx1 = cx0 + (int)(((int64_t)(qx1 - sx0) * cw) / sw);
    const int ny1 = cy0 + (int)(((int64_t)(qy1 - sy0) * ch) / sh);
    if (nx1 <= nx0 || ny1 <= ny0)
    {
        ClearSpan(src_out);
        ClearSpan(dst_out);
        return PM_OK;
    }

    src_out->x = qx0;
    src_out->y = qy0;
    src_out->w = qx1 - qx0;
    src_out->h = qy1 - qy0;
    dst_out->x = nx0;
    dst_out->y = ny0;
    dst_out->w = nx1 - nx0;
    dst_out->h = ny1 - ny0;
    return PM_OK;
}

void AlignEven(Span *span)
{
    const int x1 = span->x + span->w;
    const int y1 = span->y + span->h;
    const int x = (span->x + 1) & ~1;
    const int y = (span->y + 1) & ~1;
    const int ex = x1 & ~1;
    const int ey = y1 & ~1;
    span->x = x;
    span->y = y;
    span->w = ex - x;
    span->h = ey - y;
}

int TooSmall(const Span *span)
{
    return span->w < 2 || span->h < 2;
}

im_rect ToRect(const Span *span)
{
    im_rect rect;
    rect.x = span->x;
    rect.y = span->y;
    rect.width = span->w;
    rect.height = span->h;
    return rect;
}

int Import(const PmImage *image, RgaImage *out)
{
    const int format = RgaFormat(image->format);
    if (format < 0 || image->fd < 0 || image->size <= 0)
    {
        return PM_ERR;
    }
    out->handle = importbuffer_fd(image->fd, image->size);
    if (out->handle == 0)
    {
        return PM_ERR;
    }
    out->buf = wrapbuffer_handle(out->handle,
                                 image->width,
                                 image->height,
                                 format,
                                 image->hor_stride,
                                 image->ver_stride);
    return PM_OK;
}

uint32_t PackColor(uint32_t rgb)
{
    const unsigned r = (rgb >> 16) & 0xffu;
    const unsigned g = (rgb >> 8) & 0xffu;
    const unsigned b = rgb & 0xffu;
    /* RGA 的填色是 0xAABBGGRR。 */
    return 0xff000000u | (b << 16) | (g << 8) | r;
}

int ClipBox(const PmImage *image, const PmRect *box, Span *out)
{
    if (box->w < 0 || box->h < 0)
    {
        return PM_ERR;
    }
    if (box->w == 0 || box->h == 0)
    {
        ClearSpan(out);
        return PM_OK;
    }
    int x0 = box->x;
    int y0 = box->y;
    int x1 = box->x + box->w;
    int y1 = box->y + box->h;
    if (x0 < 0)
    {
        x0 = 0;
    }
    if (y0 < 0)
    {
        y0 = 0;
    }
    if (x1 > image->width)
    {
        x1 = image->width;
    }
    if (y1 > image->height)
    {
        y1 = image->height;
    }
    out->x = x0;
    out->y = y0;
    out->w = x1 - x0;
    out->h = y1 - y0;
    if (out->w < 0 || out->h < 0)
    {
        ClearSpan(out);
    }
    return PM_OK;
}

}

extern "C" PmStatus plt_rga_blit(const PmImage *src, const PmRect *src_rect, PmImage *dst, const PmRect *dst_rect)
{
    if (!pm_image_valid(src) || !pm_image_valid(dst))
    {
        return PM_ERR;
    }
    if (src->fd == dst->fd)
    {
        return PM_ERR;
    }
    if (RgaFormat(src->format) < 0 || RgaFormat(dst->format) < 0)
    {
        return PM_ERR;
    }

    Span src_span;
    Span dst_span;
    const int clipped = ClipMapped(FromRect(src, src_rect),
                                   src->width,
                                   src->height,
                                   FromRect(dst, dst_rect),
                                   dst->width,
                                   dst->height,
                                   &src_span,
                                   &dst_span);
    if (clipped != PM_OK)
    {
        return PM_ERR;
    }
    if (IsYuv(src->format))
    {
        AlignEven(&src_span);
    }
    if (IsYuv(dst->format))
    {
        AlignEven(&dst_span);
    }
    if (TooSmall(&src_span) || TooSmall(&dst_span))
    {
        return PM_OK;
    }

    RgaImage src_image;
    RgaImage dst_image;
    if (Import(src, &src_image) != PM_OK || Import(dst, &dst_image) != PM_OK)
    {
        fprintf(stderr, "[plt.rga] import buffer failed\n");
        return PM_ERR;
    }

    int usage = IM_SYNC;
    if (IsYuv(src->format) && !IsYuv(dst->format))
    {
        usage |= src->range == PM_RANGE_FULL ? IM_YUV_TO_RGB_BT601_FULL : IM_YUV_TO_RGB_BT601_LIMIT;
    }
    else if (!IsYuv(src->format) && IsYuv(dst->format))
    {
        usage |= dst->range == PM_RANGE_FULL ? IM_RGB_TO_YUV_BT601_FULL : IM_RGB_TO_YUV_BT601_LIMIT;
    }
    else if (IsYuv(src->format) && IsYuv(dst->format) && src->range != dst->range)
    {
        imsetColorSpace(&src_image.buf,
                        src->range == PM_RANGE_FULL ? IM_YUV_BT601_FULL_RANGE : IM_YUV_BT601_LIMIT_RANGE);
        imsetColorSpace(&dst_image.buf,
                        dst->range == PM_RANGE_FULL ? IM_YUV_BT601_FULL_RANGE : IM_YUV_BT601_LIMIT_RANGE);
    }

    rga_buffer_t empty;
    memset(&empty, 0, sizeof(empty));
    im_rect empty_rect;
    memset(&empty_rect, 0, sizeof(empty_rect));
    const IM_STATUS status = improcess(src_image.buf,
                                       dst_image.buf,
                                       empty,
                                       ToRect(&src_span),
                                       ToRect(&dst_span),
                                       empty_rect,
                                       -1,
                                       nullptr,
                                       nullptr,
                                       usage);
    if (!RgaOk(status))
    {
        fprintf(stderr, "[plt.rga] blit failed %d\n", (int)status);
        return PM_ERR;
    }
    return PM_OK;
}

extern "C" PmStatus plt_rga_boxes(PmImage *dst, const PmRect *boxes, int count, uint32_t rgb, int thickness)
{
    if (!pm_image_valid(dst) || thickness <= 0 || RgaFormat(dst->format) < 0)
    {
        return PM_ERR;
    }
    if (count < 0 || (count > 0 && boxes == nullptr))
    {
        return PM_ERR;
    }
    if (count == 0)
    {
        return PM_OK;
    }

    RgaImage image;
    if (Import(dst, &image) != PM_OK)
    {
        fprintf(stderr, "[plt.rga] import buffer failed\n");
        return PM_ERR;
    }

    const uint32_t color = PackColor(rgb);
    for (int i = 0; i < count; ++i)
    {
        Span box;
        if (ClipBox(dst, &boxes[i], &box) != PM_OK)
        {
            return PM_ERR;
        }
        if (IsYuv(dst->format))
        {
            AlignEven(&box);
        }
        if (TooSmall(&box))
        {
            continue;
        }
        const IM_STATUS status = imrectangle(image.buf, ToRect(&box), color, thickness, 1, nullptr);
        if (!RgaOk(status))
        {
            fprintf(stderr, "[plt.rga] box failed %d\n", (int)status);
            return PM_ERR;
        }
    }
    return PM_OK;
}
