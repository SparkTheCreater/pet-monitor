/**
 * @file    media_pic.c
 * @brief   缩放、拷贝、转色、画线、画框、填色、旋转、翻转和平移。都交给平台层，返回时硬件已经做完。
 */
#include "media_pic.h"

#include "plt/plt_rga.h"

BaseStatus media_pic_scale(const BaseImage *src, const BaseRect *src_rect, BaseImage *dst, const BaseRect *dst_rect)
{
    return plt_rga_blit(src, src_rect, dst, dst_rect);
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
