/**
 * @file frame.c
 * @brief 帧管理实现。
 */

#include "internal.h"

/* ── 内部: 连续 AVFrame 像素缓冲分配 ─────────────────────────────────
 *
 * 不使用 av_frame_get_buffer()。后者会按 32 行高度对齐 + 每平面 16 字节
 * padding，把 UV 推到 (data[0] + linesize[0]*align_up(H, 32) + 32)。RGA 的
 * wrapbuffer_virtualaddr_t() 只取一个基址、按 wstride*hstride 推算 UV，没
 * 有字段表达 ffmpeg 的 gap——结果就是 1080p NV12 缩放后帧底 16 行变绿。
 *
 * 这里用 av_image_get_buffer_size + av_image_fill_arrays(align=1) 强制平面
 * 紧贴，让 RGA 的偏移算式与实际内存严格一致。
 */
rkvc_err rkvc_avframe_alloc_contiguous(AVFrame *av_frame)
{
    if (!av_frame || av_frame->width <= 0 || av_frame->height <= 0)
        return RKVC_ERR_INVALID;

    enum AVPixelFormat av_fmt = (enum AVPixelFormat)av_frame->format;
    if (av_fmt == AV_PIX_FMT_NONE)
        return RKVC_ERR_INVALID;

    int buf_size = av_image_get_buffer_size(av_fmt,
                                            av_frame->width,
                                            av_frame->height, 1);
    if (buf_size < 0)
        return rkvc_from_averror(buf_size);

    AVBufferRef *buf = av_buffer_alloc(buf_size);
    if (!buf)
        return RKVC_ERR_NOMEM;

    int ret = av_image_fill_arrays(av_frame->data, av_frame->linesize,
                                   buf->data, av_fmt,
                                   av_frame->width, av_frame->height, 1);
    if (ret < 0) {
        av_buffer_unref(&buf);
        return rkvc_from_averror(ret);
    }

    av_frame->buf[0] = buf;
    /* extended_data must point at data[] for plain (non-extended) layouts. */
    av_frame->extended_data = av_frame->data;
    return RKVC_OK;
}

rkvc_err rkvc_frame_alloc(rkvc_frame **out, int width, int height,
                          rkvc_pix_fmt format)
{
    if (!out)
        return RKVC_ERR_INVALID;

    *out = NULL;

    if (width <= 0 || height <= 0 || !rkvc_is_valid_pix_fmt(format))
        return RKVC_ERR_INVALID;

    rkvc_frame *f = rkvc_calloc(1, sizeof(*f));
    if (!f)
        return RKVC_ERR_NOMEM;

    f->av_frame = av_frame_alloc();
    if (!f->av_frame) {
        rkvc_free(f);
        return RKVC_ERR_NOMEM;
    }

    f->av_frame->width  = width;
    f->av_frame->height = height;
    f->av_frame->format = rkvc_to_av_pix_fmt(format);

    rkvc_err err = rkvc_avframe_alloc_contiguous(f->av_frame);
    if (err != RKVC_OK) {
        av_frame_free(&f->av_frame);
        rkvc_free(f);
        return err;
    }

    f->ref_count = 1;
    pthread_mutex_init(&f->lock, NULL);

    f->info.width  = width;
    f->info.height = height;
    f->info.format = format;
    f->info.pts    = 0;
    f->info.key_frame = 0;

    *out = f;
    return RKVC_OK;
}

rkvc_err rkvc_frame_get_info(const rkvc_frame *f, rkvc_frame_info *info)
{
    if (!f || !info)
        return RKVC_ERR_INVALID;

    *info = f->info;
    return RKVC_OK;
}

rkvc_err rkvc_frame_get_data(const rkvc_frame *f,
                             uint8_t *planes[4], int strides[4])
{
    if (!f || !f->av_frame || !planes || !strides)
        return RKVC_ERR_INVALID;

    for (int i = 0; i < 4; i++) {
        planes[i]  = f->av_frame->data[i];
        strides[i] = f->av_frame->linesize[i];
    }
    return RKVC_OK;
}

rkvc_err rkvc_frame_set_pts(rkvc_frame *f, int64_t pts)
{
    if (!f)
        return RKVC_ERR_INVALID;

    f->av_frame->pts = pts;
    f->info.pts = pts;
    return RKVC_OK;
}

rkvc_frame *rkvc_frame_ref(rkvc_frame *f)
{
    if (!f)
        return NULL;

    pthread_mutex_lock(&f->lock);
    f->ref_count++;
    pthread_mutex_unlock(&f->lock);
    return f;
}

void rkvc_frame_unref(rkvc_frame *f)
{
    if (!f)
        return;

    pthread_mutex_lock(&f->lock);
    int do_free = (--f->ref_count <= 0);
    pthread_mutex_unlock(&f->lock);

    if (do_free) {
        av_frame_free(&f->av_frame);
        pthread_mutex_destroy(&f->lock);
        rkvc_free(f);
    }
}
