/**
 * @file    plt_stub.c
 * @brief   没有芯片时的空实现。初始化明确失败，其余接口也不假装成功。
 */
#include "plt/plt_jpeg.h"
#include "plt/plt_mem.h"
#include "plt/plt_venc.h"
#include "plt/plt_npu.h"
#include "plt/plt_rga.h"
#include "plt/plt_sys.h"
#include "plt/plt_vdec.h"

#include <string.h>

static void ClearImage(BaseImage *image)
{
    if (image == NULL)
    {
        return;
    }
    memset(image, 0, sizeof(*image));
    image->fd = -1;
}

BaseStatus plt_init(void)
{
    return BASE_ERR;
}

void plt_deinit(void)
{
}

BaseStatus plt_mem_alloc(int width, int height, int format, BaseImage *out)
{
    (void)width;
    (void)height;
    (void)format;
    ClearImage(out);
    return BASE_ERR;
}

void plt_mem_free(BaseImage *image)
{
    ClearImage(image);
}

BaseStatus plt_mem_flush(BaseImage *image)
{
    (void)image;
    return BASE_ERR;
}

BaseStatus plt_vdec_open(int codec, int buf_count, PltVdec **out)
{
    (void)codec;
    (void)buf_count;
    if (out != NULL)
    {
        *out = NULL;
    }
    return BASE_ERR;
}

void plt_vdec_close(PltVdec *dec)
{
    (void)dec;
}

BaseStatus plt_vdec_send(PltVdec *dec, const uint8_t *data, int len, int64_t pts_us, int eos)
{
    (void)dec;
    (void)data;
    (void)len;
    (void)pts_us;
    (void)eos;
    return BASE_ERR;
}

BaseStatus plt_vdec_get(PltVdec *dec, BaseDecodedFrame *frame)
{
    (void)dec;
    if (frame != NULL)
    {
        memset(frame, 0, sizeof(*frame));
        frame->image.fd = -1;
    }
    return BASE_ERR;
}

BaseStatus plt_vdec_release(PltVdec *dec, BaseDecodedFrame *frame)
{
    (void)dec;
    (void)frame;
    return BASE_ERR;
}

BaseStatus plt_rga_blit(const BaseImage *src, const BaseRect *src_rect, BaseImage *dst, const BaseRect *dst_rect)
{
    (void)src;
    (void)src_rect;
    (void)dst;
    (void)dst_rect;
    return BASE_ERR;
}

BaseStatus plt_rga_boxes(BaseImage *dst, const BaseRect *boxes, int count, uint32_t rgb, int thickness)
{
    (void)dst;
    (void)boxes;
    (void)count;
    (void)rgb;
    (void)thickness;
    return BASE_ERR;
}

BaseStatus plt_rga_fill(BaseImage *dst, const BaseRect *rect, uint32_t rgb)
{
    (void)dst;
    (void)rect;
    (void)rgb;
    return BASE_ERR;
}

BaseStatus plt_rga_rotate(const BaseImage *src, BaseImage *dst, int angle)
{
    (void)src;
    (void)dst;
    (void)angle;
    return BASE_ERR;
}

BaseStatus plt_rga_flip(const BaseImage *src, BaseImage *dst, int direction)
{
    (void)src;
    (void)dst;
    (void)direction;
    return BASE_ERR;
}

BaseStatus plt_rga_translate(const BaseImage *src, BaseImage *dst, int x, int y)
{
    (void)src;
    (void)dst;
    (void)x;
    (void)y;
    return BASE_ERR;
}

BaseStatus plt_jpeg_encode(const BaseImage *image, int quality, uint8_t **jpeg, size_t *jpeg_len)
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
    return BASE_ERR;
}

void plt_jpeg_free(uint8_t *jpeg)
{
    (void)jpeg;
}

BaseStatus plt_jpeg_decode(const uint8_t *data, size_t size, BaseImage *image)
{
    (void)data;
    (void)size;
    ClearImage(image);
    return BASE_ERR;
}

BaseStatus plt_npu_open(const char* modelPath, PltNpu** out)
{
    (void)modelPath;
    if (out != NULL)
    {
        *out = NULL;
    }
    return BASE_ERR;
}

void plt_npu_close(PltNpu* npu)
{
    (void)npu;
}

BaseStatus plt_npu_set_core(PltNpu* npu, int coreMask)
{
    (void)npu;
    (void)coreMask;
    return BASE_ERR;
}

BaseStatus plt_npu_input_info(PltNpu* npu, PltNpuTensor* info)
{
    (void)npu;
    if (info != NULL)
    {
        memset(info, 0, sizeof(*info));
    }
    return BASE_ERR;
}

BaseStatus plt_npu_output_count(PltNpu* npu, int* count)
{
    (void)npu;
    if (count != NULL)
    {
        *count = 0;
    }
    return BASE_ERR;
}

BaseStatus plt_npu_output_info(PltNpu* npu, int index, PltNpuTensor* info)
{
    (void)npu;
    (void)index;
    if (info != NULL)
    {
        memset(info, 0, sizeof(*info));
    }
    return BASE_ERR;
}

BaseStatus plt_npu_run(PltNpu* npu, const BaseImage* input, void* const* outputs, const size_t* bytes)
{
    (void)npu;
    (void)input;
    (void)outputs;
    (void)bytes;
    return BASE_ERR;
}

static int VencParamOk(const PltVencParam *param)
{
    if (param->codec != BASE_CODEC_H264 && param->codec != BASE_CODEC_H265)
    {
        return 0;
    }
    if (param->format != BASE_PIX_NV12 && param->format != BASE_PIX_RGB888)
    {
        return 0;
    }
    if (param->width <= 0 || param->height <= 0 || param->fps <= 0 || param->gop <= 0 || param->bitrate <= 0)
    {
        return 0;
    }
    if (param->rc_mode != PLT_RC_CBR && param->rc_mode != PLT_RC_VBR)
    {
        return 0;
    }
    if (param->qp_min < 0 || param->qp_max > 51 || param->qp_min > param->qp_init || param->qp_init > param->qp_max)
    {
        return 0;
    }
    if (param->codec == BASE_CODEC_H264 &&
        (param->profile != 66 && param->profile != 77 && param->profile != 100))
    {
        return 0;
    }
    if (param->codec == BASE_CODEC_H264 && (param->level < 10 || param->level > 52))
    {
        return 0;
    }
    return 1;
}

BaseStatus plt_venc_open(const PltVencParam *param, PltVenc **out)
{
    if (out != NULL)
    {
        *out = NULL;
    }
    if (param == NULL || out == NULL || !VencParamOk(param))
    {
        return BASE_ERR;
    }
    return BASE_UNSUPPORTED;
}

void plt_venc_close(PltVenc *enc)
{
    (void)enc;
}

BaseStatus plt_venc_extradata(PltVenc *enc, const uint8_t **data, size_t *size)
{
    (void)enc;
    if (data != NULL)
    {
        *data = NULL;
    }
    if (size != NULL)
    {
        *size = 0;
    }
    return BASE_ERR;
}

BaseStatus plt_venc_send(PltVenc *enc, const BaseImage *image, int64_t pts_us)
{
    (void)enc;
    (void)image;
    (void)pts_us;
    return BASE_ERR;
}

BaseStatus plt_venc_get(PltVenc *enc, PltVencPacket *packet)
{
    (void)enc;
    if (packet != NULL)
    {
        memset(packet, 0, sizeof(*packet));
    }
    return BASE_ERR;
}

void plt_venc_get_done(PltVenc *enc)
{
    (void)enc;
}

BaseStatus plt_venc_request_idr(PltVenc *enc)
{
    (void)enc;
    return BASE_ERR;
}

BaseStatus plt_venc_set_bitrate(PltVenc *enc, int bitrate)
{
    (void)enc;
    (void)bitrate;
    return BASE_ERR;
}

BaseStatus plt_venc_set_rotation(PltVenc *enc, int angle)
{
    (void)enc;
    (void)angle;
    return BASE_ERR;
}

BaseStatus plt_venc_set_mirror(PltVenc *enc, int direction)
{
    (void)enc;
    (void)direction;
    return BASE_ERR;
}

BaseStatus plt_venc_set_roi(PltVenc *enc, const PltVencRoi *regions, int count)
{
    (void)enc;
    (void)regions;
    (void)count;
    return BASE_ERR;
}

BaseStatus plt_venc_set_osd_palette(PltVenc *enc, const PltVencOsdColor *colors, int count)
{
    (void)enc;
    (void)colors;
    (void)count;
    return BASE_ERR;
}

BaseStatus plt_venc_set_osd(PltVenc *enc, const PltVencOsd *regions, int count)
{
    (void)enc;
    (void)regions;
    (void)count;
    return BASE_ERR;
}
