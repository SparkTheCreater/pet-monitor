/**
 * @file    gfx.c
 * @brief   缩放、拷贝、转色、画线和画框。都交给平台层，返回时硬件已经做完。
 */
#include "media/pm_gfx.h"

#include "plt/plt_rga.h"

PmStatus pm_gfx_scale(const PmImage *src, const PmRect *src_rect, PmImage *dst, const PmRect *dst_rect)
{
    return plt_rga_blit(src, src_rect, dst, dst_rect);
}

PmStatus pm_gfx_line(PmImage *dst, int x, int y, int length, int thickness, int horizontal, uint32_t rgb)
{
    PmRect line;
    if (!pm_image_valid(dst) || thickness <= 0)
    {
        return PM_ERR;
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

PmStatus pm_gfx_box(PmImage *dst, const PmRect *boxes, int count, uint32_t rgb, int thickness)
{
    return plt_rga_boxes(dst, boxes, count, rgb, thickness);
}
