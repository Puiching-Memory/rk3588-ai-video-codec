/**
 * @file scale.c
 * @brief RGA 2D 硬件缩放/格式转换实现。
 *
 * 基于 librga 的 im2d C API，使用虚拟地址模式操作 NV12/YUV420P 帧。
 */

#include "internal.h"

#include <rga/im2d.h>
#include <rga/rga.h>
#include <rga/RgaUtils.h>

#include <pthread.h>
#include <sys/stat.h>

/* ── RGA 可用性检测 (线程安全单例) ────────────────────────────────── */

static int g_rga_available = -1; /* -1 = 未检测 */
static pthread_once_t g_rga_once = PTHREAD_ONCE_INIT;

static void detect_rga(void)
{
    struct stat st;
    g_rga_available = (stat("/dev/rga", &st) == 0) ? 1 : 0;
}

static int rga_available(void)
{
    pthread_once(&g_rga_once, detect_rga);
    return g_rga_available;
}

int rkvc_scale_available(void)
{
    return rga_available();
}

/* ── 格式映射 ────────────────────────────────────────────────────── */

static int to_rga_format(int fmt)
{
    switch (fmt) {
    case RKVC_PIX_FMT_NV12:    return RK_FORMAT_YCbCr_420_SP;
    case RKVC_PIX_FMT_YUV420P: return RK_FORMAT_YCbCr_420_P;
    case RKVC_PIX_FMT_NV16:    return RK_FORMAT_YCbCr_422_SP;
    case RKVC_PIX_FMT_P010:    return RK_FORMAT_YCbCr_420_SP_10B;
    default:                    return RK_FORMAT_YCbCr_420_SP;
    }
}

/*
 * RGA's wrapbuffer_virtualaddr_t() takes a single base pointer and computes
 * subsequent plane offsets as `wstride * hstride * <plane_factor>`. If the
 * source AVFrame's planes have any extra padding between them (which is the
 * case for buffers from av_frame_get_buffer() and for the sw frame produced
 * by av_hwframe_transfer_data), RGA reads/writes UV at the wrong address.
 *
 * Returns 1 if the frame's planes are laid out exactly back-to-back so that
 * `wrapbuffer_virtualaddr_t(data[0], w, h, linesize[0], h, fmt)` reads/writes
 * the correct UV (and V, for I420) addresses.
 */
static int frame_is_contiguous_for_rga(const AVFrame *f)
{
    if (!f || !f->data[0])
        return 0;

    int w = f->linesize[0];
    int h = f->height;

    switch (f->format) {
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_NV21:
    case AV_PIX_FMT_NV16: {
        if (!f->data[1])
            return 0;
        return (f->data[1] == f->data[0] + (ptrdiff_t)w * h)
            && (f->linesize[1] == w);
    }
    case AV_PIX_FMT_YUV420P: {
        if (!f->data[1] || !f->data[2])
            return 0;
        ptrdiff_t y_size = (ptrdiff_t)w * h;
        ptrdiff_t u_size = (ptrdiff_t)(w / 2) * (h / 2);
        return (f->data[1] == f->data[0] + y_size)
            && (f->data[2] == f->data[1] + u_size)
            && (f->linesize[1] == w / 2)
            && (f->linesize[2] == w / 2);
    }
    case AV_PIX_FMT_P010LE:
    case AV_PIX_FMT_P010BE: {
        if (!f->data[1])
            return 0;
        return (f->data[1] == f->data[0] + (ptrdiff_t)w * h)
            && (f->linesize[1] == w);
    }
    default:
        /* Conservative: treat unknown formats as contiguous-only when
         * they have a single plane. */
        return f->data[1] == NULL;
    }
}

/* ── 帧缩放 ─────────────────────────────────────────────────────── */

rkvc_err rkvc_frame_scale(const rkvc_frame *src, rkvc_frame **out,
                          const rkvc_scale_config *cfg)
{
    if (!src || !out || !cfg)
        return RKVC_ERR_INVALID;

    *out = NULL;

    if (!src->av_frame || !src->av_frame->data[0])
        return RKVC_ERR_INVALID;

    if (cfg->dst_width <= 0 || cfg->dst_height <= 0)
        return RKVC_ERR_INVALID;

    if (!rga_available()) {
        RKVC_LOG("RGA not available (/dev/rga)");
        return RKVC_ERR_HW;
    }

    int src_w = src->av_frame->width;
    int src_h = src->av_frame->height;
    int dst_w = cfg->dst_width;
    int dst_h = cfg->dst_height;
    rkvc_pix_fmt dst_fmt = (cfg->dst_format >= 0)
                            ? (rkvc_pix_fmt)cfg->dst_format
                            : src->info.format;

    /*
     * If the source frame's planes are not laid out back-to-back (e.g. it
     * came from av_hwframe_transfer_data + av_frame_get_buffer's default
     * padding, or from a caller using ffmpeg directly), copy it into a
     * contiguous temporary frame so RGA's plane-offset arithmetic stays
     * valid. Frames produced by rkvc_frame_alloc / rkvc_decoder always
     * pass the contiguous check and skip this copy.
     */
    rkvc_frame *src_owned = NULL;
    const AVFrame *src_av = src->av_frame;

    if (!frame_is_contiguous_for_rga(src_av)) {
        rkvc_err cerr = rkvc_frame_alloc(&src_owned, src_w, src_h,
                                         src->info.format);
        if (cerr != RKVC_OK)
            return cerr;

        av_image_copy(src_owned->av_frame->data,
                      src_owned->av_frame->linesize,
                      (const uint8_t **)src_av->data,
                      src_av->linesize,
                      (enum AVPixelFormat)src_av->format,
                      src_w, src_h);

        src_av = src_owned->av_frame;
    }

    int rga_src_fmt = to_rga_format(src->info.format);
    rga_buffer_t rga_src = wrapbuffer_virtualaddr_t(
        src_av->data[0],
        src_w, src_h,
        src_av->linesize[0], src_h,
        rga_src_fmt);

    /* 分配目标帧 (contiguous, 适配 RGA 的偏移假设) */
    rkvc_frame *dst = NULL;
    rkvc_err err = rkvc_frame_alloc(&dst, dst_w, dst_h, dst_fmt);
    if (err != RKVC_OK) {
        rkvc_frame_unref(src_owned);
        return err;
    }

    /* 构造 RGA 目标 buffer */
    int rga_dst_fmt = to_rga_format(dst_fmt);
    rga_buffer_t rga_dst = wrapbuffer_virtualaddr_t(
        dst->av_frame->data[0],
        dst_w, dst_h,
        dst->av_frame->linesize[0], dst_h,
        rga_dst_fmt);

    /* 执行硬件缩放 */
    IM_STATUS ret = imresize_t(rga_src, rga_dst, 0, 0, INTER_LINEAR, 1);
    if (ret != IM_STATUS_SUCCESS) {
        RKVC_LOG("RGA imresize failed: %s", imStrError(ret));
        rkvc_frame_unref(dst);
        rkvc_frame_unref(src_owned);
        return RKVC_ERR_HW;
    }

    /* 保留源帧的 PTS */
    dst->av_frame->pts = src->av_frame->pts;
    dst->info.pts = src->info.pts;

    rkvc_frame_unref(src_owned);

    *out = dst;
    return RKVC_OK;
}
