/**
 * @file    app_mark.c
 * @brief   拷一份 NV12，用 RGA 画框，再用 RGA 填色把置信度叠上。
 */
#include "app_mark.h"

#include "media_mem.h"
#include "media_pic.h"

#include <stdio.h>
#include <string.h>

static const uint32_t kBox = 0x0000FF00u;
static const uint32_t kText = 0x00FFFFFFu;
static const uint32_t kShade = 0x00000000u;

static const uint8_t kGlyph[][7] =
{
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}
};

static int Even(int value)
{
    if (value < 0)
    {
        return 0;
    }
    return value & ~1;
}

static int GlyphOf(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch == '.')
    {
        return 10;
    }
    return -1;
}

static void Block(BaseImage *image, int x, int y, uint32_t rgb)
{
    BaseRect rect;
    rect.x = Even(x);
    rect.y = Even(y);
    rect.w = 4;
    rect.h = 4;
    if (rect.x >= image->width || rect.y >= image->height)
    {
        return;
    }
    media_pic_fill(image, &rect, rgb);
}

static void Glyph(BaseImage *image, int x, int y, int index, uint32_t rgb)
{
    int row;
    int col;
    x = Even(x);
    y = Even(y);
    for (row = 0; row < 7; row++)
    {
        uint8_t bits = kGlyph[index][row];
        for (col = 0; col < 5; col++)
        {
            if ((bits & (uint8_t)(0x10 >> col)) == 0)
            {
                continue;
            }
            Block(image, x + col * 4, y + row * 4, rgb);
        }
    }
}

static void Text(BaseImage *image, int x, int y, const char *text, uint32_t rgb)
{
    int i;
    int cursor;
    cursor = Even(x);
    y = Even(y);
    for (i = 0; text[i] != '\0'; i++)
    {
        int index = GlyphOf(text[i]);
        if (index >= 0)
        {
            Glyph(image, cursor + 4, y + 4, index, kShade);
            Glyph(image, cursor, y, index, rgb);
        }
        cursor += 4 * 6;
    }
}

static void One(BaseImage *image, const AlgoBox *box)
{
    char score[8];
    int x;
    int y;
    if (box->box.w < 4 || box->box.h < 4)
    {
        return;
    }
    snprintf(score, sizeof(score), "%.2f", (double)box->score);
    x = box->box.x;
    y = box->box.y - 7 * 4 - 8;
    if (y < 0)
    {
        y = box->box.y + 8;
    }
    Text(image, x, y, score, kText);
}

BaseStatus app_mark_cats(const BaseImage *src, const AlgoDetectResult *result, BaseImage *out)
{
    BaseRect rects[ALGO_MAX];
    int n;
    int i;
    if (out != NULL)
    {
        memset(out, 0, sizeof(*out));
        out->fd = -1;
    }
    if (src == NULL || result == NULL || out == NULL || !base_image_valid(src) || src->format != BASE_PIX_NV12)
    {
        return BASE_ERR;
    }
    if (media_mem_alloc(src->width, src->height, BASE_PIX_NV12, out) != BASE_OK)
    {
        return BASE_ERR;
    }
    out->range = src->range;
    if (media_pic_scale(src, NULL, out, NULL) != BASE_OK)
    {
        media_mem_free(out);
        return BASE_ERR;
    }
    n = result->count;
    if (n > ALGO_MAX)
    {
        n = ALGO_MAX;
    }
    if (n < 0)
    {
        n = 0;
    }
    for (i = 0; i < n; i++)
    {
        rects[i] = result->box[i].box;
    }
    if (n > 0 && media_pic_box(out, rects, n, kBox, 4) != BASE_OK)
    {
        media_mem_free(out);
        return BASE_ERR;
    }
    for (i = 0; i < n; i++)
    {
        One(out, &result->box[i]);
    }
    return BASE_OK;
}
