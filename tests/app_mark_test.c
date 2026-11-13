/**
 * @file    app_mark_test.c
 * @brief   有芯片时确认 RGA 能把猫框和置信度画进一份新的 NV12。
 */
#include "app_mark.h"
#include "media_mem.h"
#include "media_sys.h"

#include <stdio.h>
#include <string.h>

static int g_failed = 0;

static void Expect(int cond, const char *name)
{
    if (cond)
    {
        return;
    }
    fprintf(stderr, "%s\n", name);
    g_failed = 1;
}

int main(void)
{
    BaseImage src;
    BaseImage out;
    AlgoDetectResult result;
    if (media_init() != BASE_OK)
    {
        fprintf(stderr, "media_init failed\n");
        return 1;
    }
    memset(&src, 0, sizeof(src));
    memset(&out, 0, sizeof(out));
    src.fd = -1;
    out.fd = -1;
    Expect(media_mem_alloc(160, 120, BASE_PIX_NV12, &src) == BASE_OK, "alloc");
    memset(&result, 0, sizeof(result));
    result.count = 1;
    result.box[0].type = ALGO_CAT;
    result.box[0].score = 0.80f;
    result.box[0].box.x = 20;
    result.box[0].box.y = 40;
    result.box[0].box.w = 60;
    result.box[0].box.h = 40;
    Expect(app_mark_cats(&src, &result, &out) == BASE_OK, "mark");
    Expect(base_image_valid(&out) && out.fd != src.fd, "copy");
    Expect(out.format == BASE_PIX_NV12 && out.width == 160 && out.height == 120, "shape");
    media_mem_free(&out);
    media_mem_free(&src);
    media_deinit();
    return g_failed == 0 ? 0 : 1;
}
