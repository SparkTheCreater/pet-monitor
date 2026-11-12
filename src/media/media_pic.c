/**
 * @file    media_pic.c
 * @brief   缩放、预处理、拷贝、转色、画线、画框、填色、旋转、翻转和平移。都交给平台层，返回时硬件已经做完。
 */
#include "media_pic.h"

#include "plt/plt_rga.h"

/* 和 YOLO 训练时的补边灰一致，RGB 各 114。 */
static const uint32_t kPreprocessGray = 0x00727272u;

/* 内容先落到偶数。靠起点的灰边若是奇数，再缩短 2 像素，使它向上对齐到偶数。 */
static int AlignAxis(int dst, int content, int *out_content, int *out_pad)
{
    int pad;
    if (content > dst)
    {
        content = dst;
    }
    if (content >= 2)
    {
        content &= ~1;
    }
    if (content < 2)
    {
        return 0;
    }
    pad = (dst - content) / 2;
    if ((pad & 1) != 0 && content >= 4)
    {
        content -= 2;
        pad = (dst - content) / 2;
    }
    if (content < 2 || pad < 0)
    {
        return 0;
    }
    *out_content = content;
    *out_pad = pad;
    return 1;
}

static int Place(int src_w, int src_h, int dst_w, int dst_h, MediaPicPreprocess *out)
{
    int content_w;
    int content_h;
    int limit_w;
    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
    {
        return 0;
    }
    limit_w = (int64_t)src_w * dst_h >= (int64_t)src_h * dst_w;
    if (limit_w)
    {
        content_w = dst_w;
        content_h = (int)(((int64_t)src_h * dst_w) / src_w);
    }
    else
    {
        content_h = dst_h;
        content_w = (int)(((int64_t)src_w * dst_h) / src_h);
    }
    if (!AlignAxis(dst_w, content_w, &content_w, &out->x_pad) ||
        !AlignAxis(dst_h, content_h, &content_h, &out->y_pad))
    {
        return 0;
    }
    out->content_w = content_w;
    out->content_h = content_h;
    if (limit_w)
    {
        out->scale = (float)content_w / (float)src_w;
    }
    else
    {
        out->scale = (float)content_h / (float)src_h;
    }
    return 1;
}

BaseStatus media_pic_scale(const BaseImage *src, const BaseRect *src_rect, BaseImage *dst, const BaseRect *dst_rect)
{
    return plt_rga_blit(src, src_rect, dst, dst_rect);
}

BaseStatus media_pic_preprocess(const BaseImage *src, BaseImage *dst, MediaPicPreprocess *out)
{
    MediaPicPreprocess place;
    BaseRect content;
    if (!base_image_valid(src) || !base_image_valid(dst) || dst->format != BASE_PIX_RGB888)
    {
        return BASE_ERR;
    }
    if (src->fd == dst->fd)
    {
        return BASE_ERR;
    }
    if (!Place(src->width, src->height, dst->width, dst->height, &place))
    {
        return BASE_ERR;
    }
    if (media_pic_fill(dst, NULL, kPreprocessGray) != BASE_OK)
    {
        return BASE_ERR;
    }
    content.x = place.x_pad;
    content.y = place.y_pad;
    content.w = place.content_w;
    content.h = place.content_h;
    if (media_pic_scale(src, NULL, dst, &content) != BASE_OK)
    {
        return BASE_ERR;
    }
    if (out != NULL)
    {
        *out = place;
    }
    return BASE_OK;
}

BaseStatus media_pic_line(BaseImage *dst, int x, int y, int length, int thickness, int horizontal, uint32_t rgb)
{
    BaseRect line;
    if (!base_image_valid(dst) || thickness <= 0)
    {
        return BASE_ERR;
    }
    if (horizontal)
    {
        if (length <= 0)
        {
            length = dst->width - x;
        }
        line.x = x;
        line.y = y;
        line.w = length;
        line.h = thickness;
    }
    else
    {
        if (length <= 0)
        {
            length = dst->height - y;
        }
        line.x = x;
        line.y = y;
        line.w = thickness;
        line.h = length;
    }
    return plt_rga_boxes(dst, &line, 1, rgb, thickness);
}

BaseStatus media_pic_box(BaseImage *dst, const BaseRect *boxes, int count, uint32_t rgb, int thickness)
{
    return plt_rga_boxes(dst, boxes, count, rgb, thickness);
}

BaseStatus media_pic_fill(BaseImage *dst, const BaseRect *rect, uint32_t rgb)
{
    return plt_rga_fill(dst, rect, rgb);
}

BaseStatus media_pic_rotate(const BaseImage *src, BaseImage *dst, int angle)
{
    return plt_rga_rotate(src, dst, angle);
}

BaseStatus media_pic_flip(const BaseImage *src, BaseImage *dst, int direction)
{
    return plt_rga_flip(src, dst, direction);
}

BaseStatus media_pic_translate(const BaseImage *src, BaseImage *dst, int x, int y)
{
    return plt_rga_translate(src, dst, x, y);
}
