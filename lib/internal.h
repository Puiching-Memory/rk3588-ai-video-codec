/**
 * @file internal.h
 * @brief 内部共享头文件 —— 仅 lib/ 下各编译单元使用。
 */

#ifndef RKVC_INTERNAL_H
#define RKVC_INTERNAL_H

#include "rkvc/rkvc.h"

/* FFmpeg headers */
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── 内部日志 ───────────────────────────────────────────────────────── */

#ifdef RKVC_DEBUG
#define RKVC_LOG(fmt, ...) \
    fprintf(stderr, "[rkvc] " fmt "\n", ##__VA_ARGS__)
#else
#define RKVC_LOG(fmt, ...) ((void)0)
#endif

/* ── 内部工具函数 ──────────────────────────────────────────────────── */

/** FFmpeg error → rkvc_err */
rkvc_err rkvc_from_averror(int av_err);

/** 获取 FFmpeg RKMPP 硬件设备上下文 (单例, 线程安全) */
rkvc_err rkvc_get_hw_device_ctx(AVBufferRef **out);

/** rkvc_pix_fmt → AVPixelFormat */
enum AVPixelFormat rkvc_to_av_pix_fmt(rkvc_pix_fmt fmt);

/** AVPixelFormat → rkvc_pix_fmt */
rkvc_pix_fmt rkvc_from_av_pix_fmt(enum AVPixelFormat fmt);

/* ── 内部帧包装 ────────────────────────────────────────────────────── */

struct rkvc_frame {
    AVFrame        *av_frame;
    rkvc_frame_info info;
    int             ref_count;
    pthread_mutex_t lock;
};

rkvc_frame *rkvc_frame_wrap_avframe(AVFrame *av_frame);

/* ── 内部编码器 ────────────────────────────────────────────────────── */

struct rkvc_encoder {
    AVCodecContext   *codec_ctx;
    AVFormatContext  *fmt_ctx;        /* 文件模式 */
    AVStream         *av_stream;      /* 文件模式 */
    AVBufferRef      *hw_device_ctx;
    AVPacket         *pkt;
    rkvc_encoder_config config;
    int               file_mode;
    int               flushed;
    pthread_mutex_t   lock;
};

/* ── 内部解码器 ────────────────────────────────────────────────────── */

struct rkvc_decoder {
    AVCodecContext   *codec_ctx;
    AVFormatContext  *fmt_ctx;        /* 文件模式 */
    AVBufferRef      *hw_device_ctx;
    AVPacket         *pkt;
    rkvc_decoder_config config;
    int               file_mode;
    int               flushed;
    int               video_stream_idx;
    pthread_mutex_t   lock;
};

/* ── 内部流 ─────────────────────────────────────────────────────────── */

#define RKVC_STREAM_BUF_MAX 64

struct rkvc_stream {
    rkvc_stream_config  config;
    rkvc_encoder       *enc;          /* 编码流 */
    rkvc_decoder       *dec;          /* 解码流 */

    /* 环形缓冲区 */
    void               *buf[RKVC_STREAM_BUF_MAX];
    int                 buf_head;
    int                 buf_tail;
    int                 buf_count;
    pthread_mutex_t     buf_lock;
    pthread_cond_t      buf_not_full;
    pthread_cond_t      buf_not_empty;

    /* 统计 */
    rkvc_stream_stats   stats;
    int64_t             first_out_time;
    int                 finished;
};

#endif /* RKVC_INTERNAL_H */
