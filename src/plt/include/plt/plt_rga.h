/**
 * @file    plt_rga.h
 * @brief   裁剪、缩放、格式转换、画框、填色、旋转、翻转和平移。调用返回时硬件已经做完。
 */
#ifndef PLT_RGA_H
#define PLT_RGA_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * 同步。矩形空指针表示整幅。宽高相同是拷贝，不同就缩放；格式不同时顺带转色。
 * NV12 转 RGB888 按源图像的 range 选 BT.601 全范围或有限范围。
 * RGB888 转 NV12 按目标图像的 range 选系数。两边都是 NV12 且 range 不同，按 BT.601 做范围转换。
 * 目标超出 dst 的部分裁掉，源矩形按比例跟着裁；源超出 src 时同理。
 * 宽或高为 0、裁完为空、裁完不足 2 像素时什么都不画，返回 BASE_OK。宽或高为负返回 BASE_ERR。
 * 不分配缓冲，也不改两边的 handle。返回后调用方可以继续使用 dst。
 */
BaseStatus plt_rga_blit(const BaseImage *src,
                      const BaseRect *src_rect,
                      BaseImage *dst,
                      const BaseRect *dst_rect);

/*
 * 同步。在 dst 这份副本上描边，不改解码帧。rgb 是 0x00RRGGBB。thickness 是线宽，必须大于 0。
 * 超出 dst 的部分裁掉。空矩形跳过。宽或高为负返回 BASE_ERR。
 * 返回时框已经画完，dst 仍归调用方。
 */
BaseStatus plt_rga_boxes(BaseImage *dst,
                       const BaseRect *boxes,
                       int count,
                       uint32_t rgb,
                       int thickness);

/*
 * 同步。在 dst 上填一块纯色。rect 空指针表示整幅。rgb 是 0x00RRGGBB。
 * 超出 dst 的部分裁掉。宽或高为 0、裁完为空时什么都不画，返回 BASE_OK。宽或高为负返回 BASE_ERR。
 * 硬件做不了的 RGB888 细条由 CPU 补上。NV12 先对齐到偶数，对齐后不足 2 像素时跳过。
 * 不分配缓冲。返回后 dst 仍归调用方。
 */
BaseStatus plt_rga_fill(BaseImage *dst, const BaseRect *rect, uint32_t rgb);

typedef enum
{
    /* 顺时针。 */
    PLT_ROTATE_90 = 90,
    PLT_ROTATE_180 = 180,
    PLT_ROTATE_270 = 270
}
PltRotate;

typedef enum
{
    /* 左右对调。 */
    PLT_FLIP_HORIZONTAL = 0,
    /* 上下对调。 */
    PLT_FLIP_VERTICAL = 1
}
PltFlip;

/*
 * 同步。整幅旋转。90 和 270 时 dst 的宽高是 src 对调，180 时宽高相同。
 * 格式必须相同，源和目标不能是同一块缓冲。角度不是这三档时返回 BASE_ERR。
 */
BaseStatus plt_rga_rotate(const BaseImage *src, BaseImage *dst, int angle);

/*
 * 同步。整幅翻转。格式和宽高必须相同，源和目标不能是同一块缓冲。
 * direction 取 PLT_FLIP_HORIZONTAL 或 PLT_FLIP_VERTICAL。
 */
BaseStatus plt_rga_flip(const BaseImage *src, BaseImage *dst, int direction);

/*
 * 同步。把 src 平移到 dst 的 (x, y)，空出的边保持 dst 原样。
 * 宽高和格式必须相同，源和目标不能是同一块缓冲。
 * x 或 y 为负返回 BASE_ERR。起点为 1 时挪到 2。移完不足 2 像素时什么都不画，返回 BASE_OK。
 */
BaseStatus plt_rga_translate(const BaseImage *src, BaseImage *dst, int x, int y);

#ifdef __cplusplus
}
#endif

#endif
