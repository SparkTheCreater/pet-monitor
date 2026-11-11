/**
 * @file    media_mem.c
 * @brief   图像缓冲交给平台层。
 */
#include "media_mem.h"

#include "plt/plt_mem.h"

BaseStatus media_mem_alloc(int width, int height, int format, BaseImage *out)
{
    return plt_mem_alloc(width, height, format, out);
}

void media_mem_free(BaseImage *image)
{
    plt_mem_free(image);
}

BaseStatus media_mem_flush(BaseImage *image)
{
    return plt_mem_flush(image);
}
