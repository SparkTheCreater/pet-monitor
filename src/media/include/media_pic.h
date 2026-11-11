/**
 * @file    media_pic.h
 * @brief   用媒体层接口包住平台层的缩放、画线、画框、填色、旋转、翻转和平移。返回时已经画完。
 */
#ifndef MEDIA_PIC_H
#define MEDIA_PIC_H

#include "base_image.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * 同步。矩形空指针表示整幅。
 * 宽高相同是拷贝，不同是缩放。格式不同时顺带转色。
 * 不改两边的缓冲归属。宽或高为负返回 BASE_ERR，空矩形返回 BASE_OK。
 */
BaseStatus media_pic_scale(const BaseImage *src,
                           const BaseRect *src_rect,
                           BaseImage *dst,
                           const BaseRect *dst_rect);

/*
 * 同步。在 dst 上画一条横线或竖线，不另分配缓冲。
 * horizontal 为 1 时沿 x 画，length 是线长；为 0 时沿 y 画。
 * length 不大于 0 时画到图像边缘。thickness 必须大于 0。
 * rgb 是 0x00RRGGBB。
 */
BaseStatus media_pic_line(BaseImage *dst,
                          int x,
                          int y,
                          int length,
                          int thickness,
                          int horizontal,
                          uint32_t rgb);

/*
 * 同步。在 dst 上描边。thickness 必须大于 0。
 * 超出图像的部分裁掉。空矩形跳过。
 */
BaseStatus media_pic_box(BaseImage *dst,
                         const BaseRect *boxes,
                         int count,
                         uint32_t rgb,
                         int thickness);

/*
 * 同步。在 dst 上填一块纯色，不另分配缓冲。rect 空指针表示整幅。
 * rgb 是 0x00RRGGBB。超出图像的部分裁掉。空矩形返回 BASE_OK，宽或高为负返回 BASE_ERR。
 */
BaseStatus media_pic_fill(BaseImage *dst, const BaseRect *rect, uint32_t rgb);

/* 同步。angle 为 90、180 或 270，顺时针。90 和 270 时 dst 宽高是 src 对调。 */
BaseStatus media_pic_rotate(const BaseImage *src, BaseImage *dst, int angle);

/* 同步。direction 为 0 左右对调，1 上下对调。宽高必须相同。 */
BaseStatus media_pic_flip(const BaseImage *src, BaseImage *dst, int direction);

/* 同步。把 src 平移到 dst 的 (x, y)。x 或 y 为负返回 BASE_ERR。空出的边保持 dst 原样。 */
BaseStatus media_pic_translate(const BaseImage *src, BaseImage *dst, int x, int y);

#ifdef __cplusplus
}
#endif

#endif
