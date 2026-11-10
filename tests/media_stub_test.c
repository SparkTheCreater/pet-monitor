/**
 * @file    media_stub_test.c
 * @brief   没有芯片时，解码打不开，画线和编码也不假装成功。
 */
#include "media/pm_decoder.h"
#include "media/pm_encoder.h"
#include "media/pm_gfx.h"

#include <stdio.h>
#include <string.h>

static int g_failed = 0;

static void Expect(int cond, const char *name)
{
    if (cond)
    {
        return;
    }
    fprintf(stderr, "fail %s\n", name);
    g_failed = 1;
}

int main(void)
{
    PmDecoder *decoder = (PmDecoder *)1;
    PmEncoder *encoder = (PmEncoder *)1;
    PmEncoderParam param;
    PmStatus status;

    Expect(pm_decoder_open(PM_CODEC_H264, 4, NULL, 0, &decoder) == PM_ERR, "decoder open");
    Expect(decoder == NULL, "decoder cleared");
    pm_decoder_close(NULL);
    Expect(pm_decoder_send(NULL, NULL, 0, 0, 0) == PM_ERR, "decoder send");

    Expect(pm_gfx_scale(NULL, NULL, NULL, NULL) == PM_ERR, "scale");
    Expect(pm_gfx_line(NULL, 0, 0, 10, 2, 1, 0) == PM_ERR, "line");
    Expect(pm_gfx_box(NULL, NULL, 0, 0, 2) == PM_ERR, "box");

    memset(&param, 0, sizeof(param));
    Expect(pm_encoder_open(NULL, &encoder) == PM_ERR, "encoder null");
    Expect(encoder == NULL, "encoder cleared");

    param.codec = PM_CODEC_H264;
    param.format = PM_PIX_NV12;
    param.width = 640;
    param.height = 480;
    param.fps = 25;
    param.gop = 50;
    param.rc_mode = PM_RC_CBR;
    param.bitrate = 2000000;
    param.qp_init = 26;
    param.qp_min = 10;
    param.qp_max = 51;
    param.profile = 100;
    param.level = 40;
    status = pm_encoder_open(&param, &encoder);
    Expect(status == PM_UNSUPPORTED, "encoder unsupported");
    Expect(encoder == NULL, "encoder still empty");
    pm_encoder_close(NULL);
    Expect(pm_encoder_send(NULL, NULL, 0) == PM_ERR, "encoder send");
    return g_failed ? 1 : 0;
}
