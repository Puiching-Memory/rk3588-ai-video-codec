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

/*
 * AV_PIX_FMT_RKMPP — 项目内部别名，映射到 DRM PRIME 像素格式。
 * ffmpeg-rockchip 的 RKMPP 编解码器通过 DRM prime 帧处理硬件帧。
 */
#define AV_PIX_FMT_RKMPP AV_PIX_FMT_DRM_PRIME

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

/** Project-owned allocation wrappers, used to enable deterministic tests. */
void *rkvc_malloc(size_t size);
void *rkvc_calloc(size_t nmemb, size_t size);
void rkvc_free(void *ptr);

#ifdef RKVC_ENABLE_FAULT_INJECTION
/** Test-only allocation fault controls. countdown=0 fails the next wrapper allocation. */
void rkvc_test_fail_alloc_after(long countdown);
void rkvc_test_clear_faults(void);
long rkvc_test_alloc_count(void);
const char *rkvc_test_guess_muxer(const char *path);
rkvc_err rkvc_test_setup_encoder_codec(AVCodecContext *ctx,
                                       const rkvc_encoder_config *cfg);
#endif

/** 获取 FFmpeg RKMPP 硬件设备上下文 (单例, 线程安全) */
rkvc_err rkvc_get_hw_device_ctx(AVBufferRef **out);

/** 检查 RKMPP 设备和至少一种 DMA-BUF allocator 当前用户是否可访问。 */
rkvc_err rkvc_check_hw_permissions(void);

/** 校验公共枚举值是否合法。 */
int rkvc_is_valid_pix_fmt(rkvc_pix_fmt fmt);
int rkvc_is_valid_preset(rkvc_preset preset);
int rkvc_is_valid_rc_mode(rkvc_rc_mode mode);
int rkvc_is_valid_stream_dir(rkvc_stream_dir dir);

/** rkvc_pix_fmt → AVPixelFormat */
enum AVPixelFormat rkvc_to_av_pix_fmt(rkvc_pix_fmt fmt);

/** AVPixelFormat → rkvc_pix_fmt */
rkvc_pix_fmt rkvc_from_av_pix_fmt(enum AVPixelFormat fmt);

/** 检查数据开头是否明显为压缩视频码流或容器。 */
int rkvc_buffer_looks_compressed_video(const uint8_t *data, size_t size);

/* ── 内部帧包装 ────────────────────────────────────────────────────── */

struct rkvc_frame {
    AVFrame        *av_frame;
    rkvc_frame_info info;
    int             ref_count;
    pthread_mutex_t lock;
};

rkvc_frame *rkvc_frame_wrap_avframe(AVFrame *av_frame);

/**
 * @brief 给 av_frame 装上一块连续 (无 inter-plane padding) 的像素缓冲。
 *
 * 与 av_frame_get_buffer() 不同——后者会按 32 行高度对齐 + 每平面 16 字节
 * padding，导致 RGA 的 wrapbuffer_virtualaddr_t 推算 UV 偏移时错位 (帧底
 * 绿带 bug)。此函数始终输出真正连续的布局，让 RGA 的偏移算式与实际内存
 * 完全匹配。
 *
 * 调用前 av_frame->{width,height,format} 必须已设置；此函数填写
 * data/linesize/buf[0]/extended_data。
 */
rkvc_err rkvc_avframe_alloc_contiguous(AVFrame *av_frame);

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
