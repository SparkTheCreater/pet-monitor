/**
 * @file    media_pic.c
 * @brief   缩放、拷贝、转色、画线和画框。都交给平台层，返回时硬件已经做完。
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
