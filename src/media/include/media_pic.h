/**
 * @file    media_pic.h
 * @brief   用媒体层接口包住平台层的缩放、预处理、画线、画框、填色、旋转、翻转和平移。返回时已经画完。
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
 * 一次预处理的几何。送进模型的是整幅目标图，框先落在这张图上。
 * scale：源图像素乘它，得到内容在目标图上的长度。由较短的一边决定，宽高共用。
 * x_pad、y_pad：灰边靠左、靠上的宽度。另一侧一样长。除不尽时多出的 1 像素留在右或下。
 * content_w、content_h：缩放后的画面宽高，不含灰边。
 * 回到源图用同一份数：x = (x_dst - x_pad) / scale，y = (y_dst - y_pad) / scale。
 */
typedef struct MediaPicPreprocess
{
    float scale;
    int x_pad;
    int y_pad;
    int content_w;
    int content_h;
}
MediaPicPreprocess;

/*
 * 同步。推理前的预处理：等比例缩放，再在短边两侧补灰，写成整幅 dst。
 * 源宽高取 src，目标宽高取 dst。先算出能放进目标、又不拉变形的内容矩形，贴在正中。
 * 目标整幅先填 RGB 114,114,114，再把 src 缩进这块内容。像素保持 0–255。
 * dst 必须已经按目标宽高分配，格式是 RGB888。src 可以是 NV12 或 RGB888。
 * 两边不能是同一块缓冲。这里不跑模型，调用方再把 dst 交给 media_npu_run。
 * 1920×1080 放进 640×640：scale = 640/1920，内容 640×360，x_pad = 0，y_pad = 140。
 * 1080×1920 放进 640×640：左右各补 140，上下不补。目标不是正方形时，仍只补较短的两边。
 * out 可空。非空时写出这次的 scale 和灰边，后面映射框必须用这一份，不要另算。
 * 内容不足 2 像素时返回 BASE_ERR，避免送出一张纯灰图。
 */
BaseStatus media_pic_preprocess(const BaseImage *src, BaseImage *dst, MediaPicPreprocess *out);

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
