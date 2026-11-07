/**
 * @file    plt_stub.c
 * @brief   没有芯片时的空实现。初始化明确失败，其余接口也不假装成功。
 */
#include "plt/plt_jpeg.h"
#include "plt/plt_mem.h"
#include "plt/plt_npu.h"
#include "plt/plt_rga.h"
#include "plt/plt_sys.h"
#include "plt/plt_vdec.h"

#include <string.h>

static void ClearImage(PmImage *image)
{
    if (image == NULL)
    {
        return;
    }
    memset(image, 0, sizeof(*image));
    image->fd = -1;
}

PmStatus plt_init(void)
{
    return PM_ERR;
}

void plt_deinit(void)
{
}

PmStatus plt_mem_alloc(int width, int height, int format, PmImage *out)
{
    (void)width;
    (void)height;
    (void)format;
    ClearImage(out);
    return PM_ERR;
}

void plt_mem_free(PmImage *image)
{
    ClearImage(image);
}

PmStatus plt_mem_flush(PmImage *image)
{
    (void)image;
    return PM_ERR;
}

PmStatus plt_vdec_open(int codec, int buf_count, PltVdec **out)
{
    (void)codec;
    (void)buf_count;
    if (out != NULL)
    {
        *out = NULL;
    }
    return PM_ERR;
}

void plt_vdec_close(PltVdec *dec)
{
    (void)dec;
}

PmStatus plt_vdec_send(PltVdec *dec, const uint8_t *data, int len, int64_t pts_us, int eos)
{
    (void)dec;
    (void)data;
    (void)len;
    (void)pts_us;
    (void)eos;
    return PM_ERR;
}

PmStatus plt_vdec_get(PltVdec *dec, PmDecodedFrame *frame)
{
    (void)dec;
    if (frame != NULL)
    {
        memset(frame, 0, sizeof(*frame));
        frame->image.fd = -1;
    }
    return PM_ERR;
}

PmStatus plt_vdec_release(PltVdec *dec, PmDecodedFrame *frame)
{
    (void)dec;
    (void)frame;
    return PM_ERR;
}

PmStatus plt_rga_blit(const PmImage *src, const PmRect *src_rect, PmImage *dst, const PmRect *dst_rect)
{
    (void)src;
    (void)src_rect;
    (void)dst;
    (void)dst_rect;
    return PM_ERR;
}

PmStatus plt_rga_boxes(PmImage *dst, const PmRect *boxes, int count, uint32_t rgb, int thickness)
{
    (void)dst;
    (void)boxes;
    (void)count;
    (void)rgb;
    (void)thickness;
    return PM_ERR;
}

PmStatus plt_jpeg_encode(const PmImage *image, int quality, uint8_t **jpeg, size_t *jpeg_len)
{
    (void)image;
    (void)quality;
    if (jpeg != NULL)
    {
        *jpeg = NULL;
    }
    if (jpeg_len != NULL)
    {
        *jpeg_len = 0;
    }
    return PM_ERR;
}

void plt_jpeg_free(uint8_t *jpeg)
{
    (void)jpeg;
}

PmStatus plt_npu_open(const char *model_path, PltNpu **out)
{
    (void)model_path;
    if (out != NULL)
    {
        *out = NULL;
    }
    return PM_ERR;
}

void plt_npu_close(PltNpu *npu)
{
    (void)npu;
}

PmStatus plt_npu_set_core(PltNpu *npu, int core_mask)
{
    (void)npu;
    (void)core_mask;
    return PM_ERR;
}

PmStatus plt_npu_input_info(PltNpu *npu, PltNpuTensor *info)
{
    (void)npu;
    if (info != NULL)
    {
        memset(info, 0, sizeof(*info));
    }
    return PM_ERR;
}

PmStatus plt_npu_output_count(PltNpu *npu, int *count)
{
    (void)npu;
    if (count != NULL)
    {
        *count = 0;
    }
    return PM_ERR;
}

PmStatus plt_npu_output_info(PltNpu *npu, int index, PltNpuTensor *info)
{
    (void)npu;
    (void)index;
    if (info != NULL)
    {
        memset(info, 0, sizeof(*info));
    }
    return PM_ERR;
}

PmStatus plt_npu_run(PltNpu *npu, const PmImage *input, void *const *outputs, const size_t *bytes)
{
    (void)npu;
    (void)input;
    (void)outputs;
    (void)bytes;
    return PM_ERR;
}
