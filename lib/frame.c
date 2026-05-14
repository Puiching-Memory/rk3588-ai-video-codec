/**
 * @file frame.c
 * @brief 帧管理实现。
 */

#include "internal.h"

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

    int ret = av_frame_get_buffer(f->av_frame, 0);
    if (ret < 0) {
        av_frame_free(&f->av_frame);
        rkvc_free(f);
        return rkvc_from_averror(ret);
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
