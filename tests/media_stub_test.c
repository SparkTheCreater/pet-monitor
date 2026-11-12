/**
 * @file    media_stub_test.c
 * @brief   没有芯片时，解码打不开，画线和编码也不假装成功。
 */
#include "media_dec.h"
#include "media_enc.h"
#include "media_jpeg.h"
#include "media_mem.h"
#include "media_npu.h"
#include "media_pic.h"
#include "media_sys.h"

#include <stdio.h>
#include <stdlib.h>
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
    MediaNpu *npu = (MediaNpu *)1;
    MediaNpuTensor tensor;
    BaseStatus status;
    BaseImage image;
    int count;

    Expect(media_init() == BASE_ERR, "media init");
    media_deinit();

    memset(&image, 0, sizeof(image));
    image.handle = (void *)1;
    image.fd = 3;
    Expect(media_mem_alloc(16, 16, BASE_PIX_NV12, &image) == BASE_ERR, "mem alloc");
    Expect(image.handle == NULL && image.fd == -1, "mem alloc cleared");
    media_mem_free(&image);
    Expect(media_mem_flush(&image) == BASE_ERR, "mem flush");

    Expect(media_npu_open("model.rknn", &npu) == BASE_ERR, "npu open");
    Expect(npu == NULL, "npu cleared");
    media_npu_close(NULL);
    Expect(media_npu_set_core(NULL, 0) == BASE_ERR, "npu core");
    Expect(media_npu_input_info(NULL, &tensor) == BASE_ERR, "npu input");
    count = 1;
    Expect(media_npu_output_count(NULL, &count) == BASE_ERR, "npu count");
    Expect(count == 0, "npu count cleared");
    Expect(media_npu_output_info(NULL, 0, &tensor) == BASE_ERR, "npu output");
    Expect(media_npu_run(NULL, NULL, NULL, NULL) == BASE_ERR, "npu run");

    Expect(media_dec_open(BASE_CODEC_H264, 4, NULL, 0, &dec) == BASE_ERR, "decoder open");
    Expect(dec == NULL, "decoder cleared");
    media_dec_close(NULL);
    Expect(media_dec_send(NULL, NULL, 0, 0, 0) == BASE_ERR, "decoder send");

    uint8_t *jpeg = (uint8_t *)1;
    size_t jpeg_len = 1;
    Expect(media_jpeg_encode(NULL, 80, &jpeg, &jpeg_len) == BASE_ERR, "jpeg encode");
    Expect(jpeg == NULL && jpeg_len == 0, "jpeg encode cleared");
    media_jpeg_free(NULL);

    memset(&image, 0, sizeof(image));
    image.handle = (void *)1;
    image.fd = 3;
    Expect(media_jpeg_decode(NULL, 0, &image) == BASE_ERR, "jpeg decode");
    Expect(image.handle == NULL && image.fd == -1, "jpeg decode cleared");
    Expect(media_jpeg_decode(NULL, 0, NULL) == BASE_ERR, "jpeg decode null");

    Expect(media_pic_scale(NULL, NULL, NULL, NULL) == BASE_ERR, "scale");
    Expect(media_pic_preprocess(NULL, NULL, NULL) == BASE_ERR, "preprocess");
    Expect(media_pic_line(NULL, 0, 0, 10, 2, 1, 0) == BASE_ERR, "line");
    Expect(media_pic_box(NULL, NULL, 0, 0, 2) == BASE_ERR, "box");
    Expect(media_pic_fill(NULL, NULL, 0) == BASE_ERR, "fill");
    Expect(media_pic_rotate(NULL, NULL, 90) == BASE_ERR, "rotate");
    Expect(media_pic_flip(NULL, NULL, 0) == BASE_ERR, "flip");
    Expect(media_pic_translate(NULL, NULL, 0, 0) == BASE_ERR, "translate");
    Expect(media_enc_set_roi(NULL, NULL, 0) == BASE_ERR, "roi");
    Expect(media_enc_set_osd(NULL, NULL, 0) == BASE_ERR, "osd");
    Expect(media_enc_set_osd_palette(NULL, NULL, 0) == BASE_ERR, "osd palette");

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
    param.profile = 0;
    param.level = 40;
    Expect(media_enc_open(&param, &enc) == BASE_ERR, "encoder profile");
    Expect(enc == NULL, "encoder profile cleared");
    param.profile = 100;
    status = media_enc_open(&param, &enc);
    Expect(status == BASE_UNSUPPORTED, "encoder unsupported");
    Expect(enc == NULL, "encoder still empty");
    media_enc_close(NULL);
    Expect(media_enc_send(NULL, NULL, 0) == BASE_ERR, "encoder send");
    Expect(media_enc_receive(NULL, NULL) == BASE_ERR, "encoder receive");
    media_enc_packet_free(NULL);
    Expect(media_enc_request_keyframe(NULL) == BASE_ERR, "encoder key");
    const uint8_t *extra = (const uint8_t *)1;
    size_t extra_size = 1;
    Expect(media_enc_extradata(NULL, &extra, &extra_size) == BASE_ERR, "encoder extradata");
    Expect(extra == NULL && extra_size == 0, "encoder extradata cleared");
    Expect(media_enc_set_bitrate(NULL, 1000000) == BASE_ERR, "encoder bitrate");
    Expect(media_enc_set_rotation(NULL, 90) == BASE_ERR, "encoder rotation");
    Expect(media_enc_set_mirror(NULL, 1) == BASE_ERR, "encoder mirror");
    return g_failed ? 1 : 0;
}
