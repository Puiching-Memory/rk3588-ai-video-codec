/**
 * @file node_rga.c
 * @brief RGA NV12 硬件缩放（importbuffer → imcheck → imresize → release）。
 */

#include "internal.h"

#include <rga/im2d.h>
#include <rga/rga.h>
#include <rga/RgaUtils.h>
#include <pthread.h>
#include <sys/stat.h>

static int g_rga_available = -1;
static pthread_once_t g_rga_once = PTHREAD_ONCE_INIT;

static void detect_rga(void)
{
    struct stat st;
    g_rga_available = (stat("/dev/rga", &st) == 0) ? 1 : 0;
}

int rkvc_rga_available(void)
{
    pthread_once(&g_rga_once, detect_rga);
    return g_rga_available;
}

static int frame_contiguous(const AVFrame *f)
{
    if (!f || !f->data[0] || f->format != AV_PIX_FMT_NV12 || !f->data[1])
        return 0;
    return (f->data[1] == f->data[0] + (ptrdiff_t)f->linesize[0] * f->height)
        && (f->linesize[1] == f->linesize[0]);
}

static int rga_nv12_stride_ok(int wstride, int height)
{
    if (wstride <= 0 || height <= 0)
        return 0;
    if ((wstride & 1) || (height & 1))
        return 0;
    if ((wstride & 15) != 0)
        return 0;
    return 1;
}

static IM_SCALE_MODE rkvc_upscale_to_rga_mode(rkvc_upscale_algo algo)
{
    switch (algo) {
    case RKVC_UPSCALE_NEAREST:  return INTER_NEAREST;
    case RKVC_UPSCALE_BICUBIC:  return INTER_CUBIC;
    case RKVC_UPSCALE_BILINEAR:
    default:                    return INTER_LINEAR;
    }
}

typedef struct {
    rga_buffer_handle_t handle;
    int                 owned;
} rga_import_t;

static void rga_import_release(rga_import_t *imp)
{
    if (imp && imp->owned && imp->handle)
        releasebuffer_handle(imp->handle);
    if (imp) {
        imp->handle = 0;
        imp->owned  = 0;
    }
}

static rkvc_err rga_import_nv12_buffer(const rkvc_buffer *buf, int w, int h,
                                       int wstride, int hstride,
                                       rga_import_t *imp, rga_buffer_t *rga)
{
    if (!buf || !buf->av_frame || !imp || !rga)
        return RKVC_ERR_INVALID;
    if (!rga_nv12_stride_ok(wstride, hstride))
        return RKVC_ERR_FORMAT;

    memset(imp, 0, sizeof(*imp));
    memset(rga, 0, sizeof(*rga));

    const int fmt = RK_FORMAT_YCbCr_420_SP;
    im_handle_param_t param = {
        .width  = (uint32_t)w,
        .height = (uint32_t)h,
        .format = (uint32_t)fmt,
    };

    if (buf->mem_type == RKVC_MEM_DMABUF && buf->fd >= 0) {
        imp->handle = importbuffer_fd(buf->fd, &param);
    } else if (frame_contiguous(buf->av_frame)) {
        imp->handle = importbuffer_virtualaddr(buf->av_frame->data[0], &param);
    } else {
        return RKVC_ERR_FORMAT;
    }

    if (!imp->handle)
        return RKVC_ERR_HW;

    imp->owned = 1;
    *rga = wrapbuffer_handle_t(imp->handle, w, h, wstride, hstride, fmt);
    return RKVC_OK;
}

static rkvc_err nv12_copy_contiguous(const rkvc_buffer *src, rkvc_buffer **dst)
{
    if (!src->av_frame || src->format != RKVC_PIX_FMT_NV12)
        return RKVC_ERR_FORMAT;

    rkvc_buffer *out = NULL;
    rkvc_err err = rkvc_buffer_pool_alloc_video(NULL, &out,
                                                src->av_frame->width,
                                                src->av_frame->height,
                                                RKVC_PIX_FMT_NV12,
                                                RKVC_MEM_DMABUF);
    if (err != RKVC_OK) {
        err = rkvc_buffer_alloc_video_host(&out,
                                           src->av_frame->width,
                                           src->av_frame->height,
                                           RKVC_PIX_FMT_NV12);
        if (err != RKVC_OK)
            return err;
    }

    const AVFrame *s = src->av_frame;
    AVFrame *d = out->av_frame;
    av_image_copy(d->data, d->linesize,
                  (const uint8_t *const *)s->data, s->linesize,
                  AV_PIX_FMT_NV12, s->width, s->height);

    out->pts = src->pts;
    *dst = out;
    return RKVC_OK;
}

static rkvc_err rga_resize_checked(const rga_buffer_t *src, const rga_buffer_t *dst,
                                   IM_SCALE_MODE mode)
{
    im_rect src_rect = {0, 0, src->width, src->height};
    im_rect dst_rect = {0, 0, dst->width, dst->height};
    rga_buffer_t pat;
    im_rect pat_rect;

    memset(&pat, 0, sizeof(pat));
    memset(&pat_rect, 0, sizeof(pat_rect));

    IM_STATUS chk = imcheck_t(*src, *dst, pat, src_rect, dst_rect, pat_rect, 0);
    if (chk != IM_STATUS_NOERROR) {
        RKVC_LOG("RGA imcheck failed: %s", imStrError_t(chk));
        return RKVC_ERR_HW;
    }

    IM_STATUS ret = imresize_t(*src, *dst, 0, 0, mode, 1);
    if (ret != IM_STATUS_SUCCESS) {
        RKVC_LOG("RGA imresize failed: %s", imStrError_t(ret));
        return RKVC_ERR_HW;
    }
    return RKVC_OK;
}

static rkvc_err rga_scale_nv12_buffers(const rkvc_buffer *src, rkvc_buffer *dst,
                                       IM_SCALE_MODE mode)
{
    const AVFrame *sf = src->av_frame;
    const AVFrame *df = dst->av_frame;
    const int sw = sf->width;
    const int sh = sf->height;
    const int dw = df->width;
    const int dh = df->height;

    rga_import_t src_imp = {0};
    rga_import_t dst_imp = {0};
    rga_buffer_t rga_src;
    rga_buffer_t rga_dst;
    rkvc_err err;

    err = rga_import_nv12_buffer(src, sw, sh, sf->linesize[0], sh,
                                 &src_imp, &rga_src);
    if (err != RKVC_OK)
        goto done;

    err = rga_import_nv12_buffer(dst, dw, dh, df->linesize[0], dh,
                                 &dst_imp, &rga_dst);
    if (err != RKVC_OK)
        goto done;

    err = rga_resize_checked(&rga_src, &rga_dst, mode);

done:
    rga_import_release(&src_imp);
    rga_import_release(&dst_imp);
    return err;
}

rkvc_err rkvc_rga_scale_buffer(const rkvc_buffer *src, rkvc_buffer **dst,
                               int dst_w, int dst_h, rkvc_pix_fmt dst_fmt,
                               rkvc_upscale_algo algo)
{
    if (!src || !dst || dst_w <= 0 || dst_h <= 0)
        return RKVC_ERR_INVALID;
    if (!rkvc_rga_available())
        return RKVC_ERR_HW;

    *dst = NULL;

    if (src->kind != RKVC_BUF_VIDEO)
        return RKVC_ERR_INVALID;

    if (!src->av_frame)
        return RKVC_ERR_INVALID;

    const int sw = src->av_frame->width;
    const int sh = src->av_frame->height;
    const rkvc_pix_fmt src_fmt = src->format;

    if (sw == dst_w && sh == dst_h && src_fmt == dst_fmt) {
        *dst = rkvc_buffer_ref((rkvc_buffer *)src);
        return RKVC_OK;
    }

    if (src_fmt != RKVC_PIX_FMT_NV12 || dst_fmt != RKVC_PIX_FMT_NV12)
        return RKVC_ERR_FORMAT;

    rkvc_buffer *work = NULL;
    const rkvc_buffer *rga_src = src;

    if (src->mem_type != RKVC_MEM_DMABUF && !frame_contiguous(src->av_frame)) {
        rkvc_err err = nv12_copy_contiguous(src, &work);
        if (err != RKVC_OK)
            return err;
        rga_src = work;
    }

    if (!rga_nv12_stride_ok(rga_src->av_frame->linesize[0], sh) ||
        !rga_nv12_stride_ok(dst_w, dst_h)) {
        rkvc_buffer_unref(work);
        return RKVC_ERR_FORMAT;
    }

    rkvc_buffer *out = NULL;
    rkvc_err err = rkvc_buffer_pool_alloc_video(NULL, &out, dst_w, dst_h,
                                                RKVC_PIX_FMT_NV12,
                                                RKVC_MEM_DMABUF);
    if (err != RKVC_OK) {
        err = rkvc_buffer_alloc_video_host(&out, dst_w, dst_h,
                                           RKVC_PIX_FMT_NV12);
        if (err != RKVC_OK) {
            rkvc_buffer_unref(work);
            return err;
        }
    }

    err = rga_scale_nv12_buffers(rga_src, out, rkvc_upscale_to_rga_mode(algo));
    rkvc_buffer_unref(work);

    if (err != RKVC_OK) {
        rkvc_buffer_unref(out);
        return err;
    }

    out->pts = src->pts;
    *dst = out;
    return RKVC_OK;
}

static void yuv420p_to_nv12_frame(const AVFrame *src, AVFrame *dst)
{
    const int w = src->width;
    const int h = src->height;
    for (int y = 0; y < h; y++)
        memcpy(dst->data[0] + y * dst->linesize[0],
               src->data[0] + y * src->linesize[0], (size_t)w);

    const int ch = h / 2;
    const int cw = w / 2;
    for (int y = 0; y < ch; y++) {
        uint8_t *d = dst->data[1] + y * dst->linesize[1];
        const uint8_t *u = src->data[1] + y * src->linesize[1];
        const uint8_t *v = src->data[2] + y * src->linesize[2];
        for (int x = 0; x < cw; x++) {
            d[2 * x]     = u[x];
            d[2 * x + 1] = v[x];
        }
    }
}

static void nv12_to_yuv420p_frame(const AVFrame *src, AVFrame *dst)
{
    const int w = src->width;
    const int h = src->height;
    for (int y = 0; y < h; y++)
        memcpy(dst->data[0] + y * dst->linesize[0],
               src->data[0] + y * src->linesize[0], (size_t)w);

    const int ch = h / 2;
    const int cw = w / 2;
    for (int y = 0; y < ch; y++) {
        const uint8_t *s = src->data[1] + y * src->linesize[1];
        uint8_t *u = dst->data[1] + y * dst->linesize[1];
        uint8_t *v = dst->data[2] + y * dst->linesize[2];
        for (int x = 0; x < cw; x++) {
            u[x] = s[2 * x];
            v[x] = s[2 * x + 1];
        }
    }
}

rkvc_err rkvc_upscale_yuv420p(const uint8_t *src, uint8_t *dst,
                              int src_w, int src_h,
                              int dst_w, int dst_h,
                              rkvc_upscale_algo algo)
{
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
        return RKVC_ERR_INVALID;
    if (algo == RKVC_UPSCALE_NONE)
        return RKVC_ERR_INVALID;
    if (!rkvc_rga_available())
        return RKVC_ERR_HW;

    AVFrame yuv_src;
    AVFrame yuv_dst;
    memset(&yuv_src, 0, sizeof(yuv_src));
    memset(&yuv_dst, 0, sizeof(yuv_dst));
    yuv_src.format = AV_PIX_FMT_YUV420P;
    yuv_src.width  = src_w;
    yuv_src.height = src_h;
    yuv_dst.format = AV_PIX_FMT_YUV420P;
    yuv_dst.width  = dst_w;
    yuv_dst.height = dst_h;

    if (av_image_fill_arrays(yuv_src.data, yuv_src.linesize, src,
                             AV_PIX_FMT_YUV420P, src_w, src_h, 1) < 0 ||
        av_image_fill_arrays(yuv_dst.data, yuv_dst.linesize, dst,
                             AV_PIX_FMT_YUV420P, dst_w, dst_h, 1) < 0)
        return RKVC_ERR_FORMAT;

    rkvc_buffer *nv12_src = NULL;
    rkvc_buffer *scaled = NULL;
    rkvc_err err = rkvc_buffer_alloc_video_host(&nv12_src, src_w, src_h,
                                                RKVC_PIX_FMT_NV12);
    if (err != RKVC_OK)
        return err;

    yuv420p_to_nv12_frame(&yuv_src, nv12_src->av_frame);
    err = rkvc_rga_scale_buffer(nv12_src, &scaled, dst_w, dst_h,
                                RKVC_PIX_FMT_NV12, algo);
    if (err == RKVC_OK)
        nv12_to_yuv420p_frame(scaled->av_frame, &yuv_dst);

    rkvc_buffer_unref(scaled);
    rkvc_buffer_unref(nv12_src);
    return err;
}
