/**
 * @file utils.c
 * @brief 内部工具函数实现。
 */

#include "internal.h"
#include <pthread.h>

/* ── FFmpeg 错误码映射 ─────────────────────────────────────────────── */

rkvc_err rkvc_from_averror(int av_err)
{
    if (av_err >= 0)
        return RKVC_OK;

    switch (av_err) {
    case AVERROR(ENOMEM):     return RKVC_ERR_NOMEM;
    case AVERROR(EINVAL):     return RKVC_ERR_INVALID;
    case AVERROR(ENOENT):     return RKVC_ERR_NOT_FOUND;
    case AVERROR(EIO):        return RKVC_ERR_IO;
    case AVERROR_EOF:         return RKVC_ERR_EOF;
    case AVERROR(EAGAIN):     return RKVC_ERR_AGAIN;
    case AVERROR(ENODEV):     return RKVC_ERR_HW;
    case AVERROR_UNKNOWN:     return RKVC_ERR_INTERNAL;
    default:
        /* AVERROR_DECODER_NOT_FOUND / ENOMEM 等 */
        if (av_err == AVERROR_DECODER_NOT_FOUND ||
            av_err == AVERROR_ENCODER_NOT_FOUND)
            return RKVC_ERR_NOT_FOUND;
        return RKVC_ERR_INTERNAL;
    }
}

/* ── RKMPP 硬件设备上下文单例 ──────────────────────────────────────── */

static AVBufferRef *s_hw_device_ctx = NULL;
static pthread_mutex_t s_hw_lock = PTHREAD_MUTEX_INITIALIZER;
static int s_hw_init_ok = 0;

rkvc_err rkvc_get_hw_device_ctx(AVBufferRef **out)
{
    if (!out)
        return RKVC_ERR_INVALID;

    pthread_mutex_lock(&s_hw_lock);

    if (!s_hw_init_ok) {
        int ret = av_hwdevice_ctx_create(&s_hw_device_ctx,
                                         AV_HWDEVICE_TYPE_RKMPP,
                                         NULL, NULL, 0);
        if (ret < 0) {
            RKVC_LOG("RKMPP hw device create failed: %s", av_err2str(ret));
            s_hw_device_ctx = NULL;
            pthread_mutex_unlock(&s_hw_lock);
            return rkvc_from_averror(ret);
        }
        s_hw_init_ok = 1;
        RKVC_LOG("RKMPP hw device created");
    }

    *out = av_buffer_ref(s_hw_device_ctx);
    pthread_mutex_unlock(&s_hw_lock);

    if (!*out)
        return RKVC_ERR_NOMEM;
    return RKVC_OK;
}

int rkvc_is_valid_pix_fmt(rkvc_pix_fmt fmt)
{
    switch (fmt) {
    case RKVC_PIX_FMT_NV12:
    case RKVC_PIX_FMT_YUV420P:
    case RKVC_PIX_FMT_NV16:
    case RKVC_PIX_FMT_P010:
        return 1;
    default:
        return 0;
    }
}

int rkvc_is_valid_preset(rkvc_preset preset)
{
    switch (preset) {
    case RKVC_PRESET_FAST:
    case RKVC_PRESET_MEDIUM:
    case RKVC_PRESET_SLOW:
        return 1;
    default:
        return 0;
    }
}

int rkvc_is_valid_rc_mode(rkvc_rc_mode mode)
{
    switch (mode) {
    case RKRC_CBR:
    case RKRC_VBR:
    case RKRC_CQP:
        return 1;
    default:
        return 0;
    }
}

int rkvc_is_valid_stream_dir(rkvc_stream_dir dir)
{
    switch (dir) {
    case RKVC_STREAM_ENCODE:
    case RKVC_STREAM_DECODE:
        return 1;
    default:
        return 0;
    }
}

/* ── 像素格式转换 ──────────────────────────────────────────────────── */

enum AVPixelFormat rkvc_to_av_pix_fmt(rkvc_pix_fmt fmt)
{
    switch (fmt) {
    case RKVC_PIX_FMT_NV12:    return AV_PIX_FMT_NV12;
    case RKVC_PIX_FMT_YUV420P: return AV_PIX_FMT_YUV420P;
    case RKVC_PIX_FMT_NV16:    return AV_PIX_FMT_NV16;
    case RKVC_PIX_FMT_P010:    return AV_PIX_FMT_P010;
    default:                    return AV_PIX_FMT_NONE;
    }
}

rkvc_pix_fmt rkvc_from_av_pix_fmt(enum AVPixelFormat fmt)
{
    switch (fmt) {
    case AV_PIX_FMT_NV12:    return RKVC_PIX_FMT_NV12;
    case AV_PIX_FMT_YUV420P: return RKVC_PIX_FMT_YUV420P;
    case AV_PIX_FMT_NV16:    return RKVC_PIX_FMT_NV16;
    case AV_PIX_FMT_P010:    return RKVC_PIX_FMT_P010;
    default:                  return RKVC_PIX_FMT_NV12;
    }
}

/* ── 帧包装 ────────────────────────────────────────────────────────── */

rkvc_frame *rkvc_frame_wrap_avframe(AVFrame *av_frame)
{
    if (!av_frame)
        return NULL;

    rkvc_frame *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;

    f->av_frame  = av_frame;
    f->ref_count = 1;
    pthread_mutex_init(&f->lock, NULL);

    /* 填充元数据 */
    f->info.width     = av_frame->width;
    f->info.height    = av_frame->height;
    f->info.format    = rkvc_from_av_pix_fmt(av_frame->format);
    f->info.pts       = av_frame->pts;
    f->info.key_frame = (av_frame->flags & AV_FRAME_FLAG_KEY) != 0;

    return f;
}
