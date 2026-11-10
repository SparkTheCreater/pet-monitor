/**
 * @file    media_stub_test.c
 * @brief   没有芯片时，解码打不开，画线和编码也不假装成功。
 */
#include "media_dec.h"
#include "media_enc.h"
#include "media_jpeg.h"
#include "media_pic.h"

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
    MediaDec *dec = (MediaDec *)1;
    MediaEnc *enc = (MediaEnc *)1;
    MediaEncParam param;
    BaseStatus status;

    Expect(media_dec_open(BASE_CODEC_H264, 4, NULL, 0, &dec) == BASE_ERR, "decoder open");
    Expect(dec == NULL, "decoder cleared");
    media_dec_close(NULL);
    Expect(media_dec_send(NULL, NULL, 0, 0, 0) == BASE_ERR, "decoder send");

    uint8_t *jpeg = (uint8_t *)1;
    size_t jpeg_len = 1;
    Expect(media_jpeg_encode(NULL, 80, &jpeg, &jpeg_len) == BASE_ERR, "jpeg encode");
    Expect(jpeg == NULL && jpeg_len == 0, "jpeg encode cleared");
    media_jpeg_free(NULL);

    Expect(media_pic_scale(NULL, NULL, NULL, NULL) == BASE_ERR, "scale");
    Expect(media_pic_line(NULL, 0, 0, 10, 2, 1, 0) == BASE_ERR, "line");
    Expect(media_pic_box(NULL, NULL, 0, 0, 2) == BASE_ERR, "box");
    Expect(media_pic_rotate(NULL, NULL, 90) == BASE_ERR, "rotate");
    Expect(media_pic_flip(NULL, NULL, 0) == BASE_ERR, "flip");
    Expect(media_pic_translate(NULL, NULL, 0, 0) == BASE_ERR, "translate");

    memset(&param, 0, sizeof(param));
    Expect(media_enc_open(NULL, &enc) == BASE_ERR, "encoder null");
    Expect(enc == NULL, "encoder cleared");

    param.codec = BASE_CODEC_H264;
    param.format = BASE_PIX_NV12;
    param.width = 640;
    param.height = 480;
    param.fps = 25;
    param.gop = 50;
    param.rc_mode = MEDIA_RC_CBR;
    param.bitrate = 2000000;
    param.qp_init = 26;
    param.qp_min = 10;
    param.qp_max = 51;
    param.profile = 100;
    param.level = 40;
    status = media_enc_open(&param, &enc);
    Expect(status == BASE_UNSUPPORTED, "encoder unsupported");
    Expect(enc == NULL, "encoder still empty");
    media_enc_close(NULL);
    Expect(media_enc_send(NULL, NULL, 0) == BASE_ERR, "encoder send");
    return g_failed ? 1 : 0;
}
