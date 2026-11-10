/**
 * @file    media_jpeg.c
 * @brief   JPEG 编码交给平台层。这里不填质量默认值。
 */
#include "media_jpeg.h"

#include "plt/plt_jpeg.h"

BaseStatus media_jpeg_encode(const BaseImage *image, int quality, uint8_t **jpeg, size_t *jpeg_len)
{
    return plt_jpeg_encode(image, quality, jpeg, jpeg_len);
}

void media_jpeg_free(uint8_t *jpeg)
{
    plt_jpeg_free(jpeg);
}
