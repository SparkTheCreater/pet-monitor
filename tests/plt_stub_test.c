/**
 * @file    plt_stub_test.c
 * @brief   没有芯片时，初始化失败，其余调用也失败，并且不崩。
 */
#include "plt/plt_jpeg.h"
#include "plt/plt_mem.h"
#include "plt/plt_npu.h"
#include "plt/plt_rga.h"
#include "plt/plt_sys.h"
#include "plt/plt_vdec.h"

#include <stdio.h>

static int g_failed = 0;

static void ExpectErr(BaseStatus status, const char *name)
{
    if (status == BASE_ERR)
    {
        return;
    }
    fprintf(stderr, "%s returned %d, want BASE_ERR\n", name, (int)status);
    g_failed = 1;
}

int main(void)
{
    ExpectErr(plt_init(), "plt_init");
    plt_deinit();

    BaseImage image;
    ExpectErr(plt_mem_alloc(16, 16, BASE_PIX_NV12, &image), "plt_mem_alloc");
    if (base_image_valid(&image))
    {
        fprintf(stderr, "alloc left a valid image\n");
        g_failed = 1;
    }
    plt_mem_free(&image);
    ExpectErr(plt_mem_flush(&image), "plt_mem_flush");

    PltVdec *dec = (PltVdec *)1;
    ExpectErr(plt_vdec_open(BASE_CODEC_H264, 4, &dec), "plt_vdec_open");
    if (dec != NULL)
    {
        fprintf(stderr, "vdec open left a handle\n");
        g_failed = 1;
    }
    plt_vdec_close(dec);
    ExpectErr(plt_vdec_send(NULL, NULL, 0, 0, 0), "plt_vdec_send");
    BaseDecodedFrame frame;
    ExpectErr(plt_vdec_get(NULL, &frame), "plt_vdec_get");
    ExpectErr(plt_vdec_release(NULL, &frame), "plt_vdec_release");

    ExpectErr(plt_rga_blit(NULL, NULL, NULL, NULL), "plt_rga_blit");
    ExpectErr(plt_rga_boxes(NULL, NULL, 0, 0, 1), "plt_rga_boxes");

    uint8_t *jpeg = (uint8_t *)1;
    size_t jpeg_len = 1;
    ExpectErr(plt_jpeg_encode(NULL, 80, &jpeg, &jpeg_len), "plt_jpeg_encode");
    if (jpeg != NULL || jpeg_len != 0)
    {
        fprintf(stderr, "jpeg encode left an output\n");
        g_failed = 1;
    }
    plt_jpeg_free(NULL);

    PltNpu *npu = (PltNpu *)1;
    ExpectErr(plt_npu_open("model.rknn", &npu), "plt_npu_open");
    if (npu != NULL)
    {
        fprintf(stderr, "npu open left a handle\n");
        g_failed = 1;
    }
    plt_npu_close(NULL);
    ExpectErr(plt_npu_set_core(NULL, 0), "plt_npu_set_core");
    PltNpuTensor tensor;
    ExpectErr(plt_npu_input_info(NULL, &tensor), "plt_npu_input_info");
    int count = 1;
    ExpectErr(plt_npu_output_count(NULL, &count), "plt_npu_output_count");
    if (count != 0)
    {
        fprintf(stderr, "output count was not cleared\n");
        g_failed = 1;
    }
    ExpectErr(plt_npu_output_info(NULL, 0, &tensor), "plt_npu_output_info");
    ExpectErr(plt_npu_run(NULL, NULL, NULL, NULL), "plt_npu_run");

    if (g_failed)
    {
        return 1;
    }
    return 0;
}
