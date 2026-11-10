/**
 * @file    plt_rga.h
 * @brief   裁剪、缩放、格式转换和画框。调用返回时硬件已经做完。
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

#ifdef __cplusplus
}
#endif

#endif
