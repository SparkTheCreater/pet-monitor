/**
 * @file    media_dec.c
 * @brief   视频解码。参数集先转成 Annex B，色彩范围留在帧上。
 */
#include "media_dec.h"

#include "plt/plt_vdec.h"

#include <stdlib.h>
#include <string.h>

struct MediaDec
{
    PltVdec *dec;
    uint8_t *extra;
    int extra_size;
    int extra_pending;
};

static int IsAnnexb(const uint8_t *data, size_t size)
{
    return size >= 4 && data[0] == 0 && data[1] == 0 &&
           (data[2] == 1 || (data[2] == 0 && data[3] == 1));
}

static int ReadNal(const uint8_t *extra, size_t size, size_t *pos, uint8_t **out, int *out_size, int *out_cap)
{
    int len;
    int need;
    uint8_t *grown;
    if (*pos + 2 > size)
    {
        return 0;
    }
    len = (extra[*pos] << 8) | extra[*pos + 1];
    *pos += 2;
    if (len <= 0 || *pos + (size_t)len > size)
    {
        return 0;
    }
    need = *out_size + 4 + len;
    if (need > *out_cap)
    {
        grown = (uint8_t *)realloc(*out, (size_t)need);
        if (grown == NULL)
        {
            return 0;
        }
        *out = grown;
        *out_cap = need;
    }
    (*out)[*out_size] = 0;
    (*out)[*out_size + 1] = 0;
    (*out)[*out_size + 2] = 0;
    (*out)[*out_size + 3] = 1;
    memcpy(*out + *out_size + 4, extra + *pos, (size_t)len);
    *out_size += 4 + len;
    *pos += (size_t)len;
    return 1;
}

static int CopyRaw(const uint8_t *extra, size_t size, uint8_t **out, int *out_size)
{
    uint8_t *buf = (uint8_t *)malloc(size);
    if (buf == NULL)
    {
        return 0;
    }
    memcpy(buf, extra, size);
    *out = buf;
    *out_size = (int)size;
    return 1;
}

/* 已经是起始码，或认不出 AVCC，就原样拷贝。转换中途失败也退回原样。 */
static int ToAnnexb(const uint8_t *extra, size_t size, uint8_t **out, int *out_size)
{
    uint8_t *buf;
    int cap;
    size_t pos;
    int count;
    int i;
    *out = NULL;
    *out_size = 0;
    if (extra == NULL || size == 0)
    {
        return 1;
    }
    if (IsAnnexb(extra, size) || size < 7 || extra[0] != 1)
    {
        return CopyRaw(extra, size, out, out_size);
    }
    buf = NULL;
    cap = 0;
    pos = 5;
    count = extra[pos] & 0x1f;
    pos++;
    for (i = 0; i < count; ++i)
    {
        if (!ReadNal(extra, size, &pos, &buf, out_size, &cap))
        {
            free(buf);
            *out_size = 0;
            return CopyRaw(extra, size, out, out_size);
        }
    }
    if (pos < size)
    {
        count = extra[pos];
        pos++;
        for (i = 0; i < count; ++i)
        {
            if (!ReadNal(extra, size, &pos, &buf, out_size, &cap))
            {
                free(buf);
                *out_size = 0;
                return CopyRaw(extra, size, out, out_size);
            }
        }
    }
    if (*out_size == 0)
    {
        free(buf);
        return CopyRaw(extra, size, out, out_size);
    }
    *out = buf;
    return 1;
}

BaseStatus media_dec_open(int codec,
                         int buf_count,
                         const uint8_t *extradata,
                         size_t extradata_size,
                         MediaDec **out)
{
    MediaDec *decoder;
    if (out != NULL)
    {
        *out = NULL;
    }
    if (out == NULL || buf_count <= 0)
    {
        return BASE_ERR;
    }
    if (codec != BASE_CODEC_H264 && codec != BASE_CODEC_H265)
    {
        return BASE_ERR;
    }
    decoder = (MediaDec *)calloc(1, sizeof(*decoder));
    if (decoder == NULL)
    {
        return BASE_ERR;
    }
    if (!ToAnnexb(extradata, extradata_size, &decoder->extra, &decoder->extra_size))
    {
        free(decoder);
        return BASE_ERR;
    }
    decoder->extra_pending = decoder->extra_size > 0;
    if (plt_vdec_open(codec, buf_count, &decoder->dec) != BASE_OK)
    {
        free(decoder->extra);
        free(decoder);
        return BASE_ERR;
    }
    *out = decoder;
    return BASE_OK;
}

void media_dec_close(MediaDec *decoder)
{
    if (decoder == NULL)
    {
        return;
    }
    if (decoder->dec != NULL)
    {
        plt_vdec_close(decoder->dec);
    }
    free(decoder->extra);
    free(decoder);
}

BaseStatus media_dec_send(MediaDec *decoder, const uint8_t *data, int len, int64_t pts_us, int eos)
{
    BaseStatus status;
    if (decoder == NULL || decoder->dec == NULL)
    {
        return BASE_ERR;
    }
    if (decoder->extra_pending)
    {
        status = plt_vdec_send(decoder->dec, decoder->extra, decoder->extra_size, pts_us, 0);
        if (status == BASE_AGAIN)
        {
            return BASE_AGAIN;
        }
        if (status != BASE_OK)
        {
            return status;
        }
        decoder->extra_pending = 0;
    }
    return plt_vdec_send(decoder->dec, data, len, pts_us, eos);
}

BaseStatus media_dec_receive(MediaDec *decoder, BaseDecodedFrame *frame)
{
    if (decoder == NULL || decoder->dec == NULL)
    {
        return BASE_ERR;
    }
    return plt_vdec_get(decoder->dec, frame);
}

BaseStatus media_dec_release(MediaDec *decoder, BaseDecodedFrame *frame)
{
    if (decoder == NULL || decoder->dec == NULL)
    {
        return BASE_ERR;
    }
    return plt_vdec_release(decoder->dec, frame);
}
