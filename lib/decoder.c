/**
 * @file decoder.c
 * @brief H.265 (HEVC) RKMPP 硬件解码器实现。
 *
 * 核心流程:
 *   open  → send_packet / read_packet → receive_frame → drain → close
 *
 * 文件模式下自动创建 demuxer 并探测流信息。
 */

#include "internal.h"

/* ── 默认配置 ──────────────────────────────────────────────────────── */

rkvc_decoder_config rkvc_decoder_config_defaults(void)
{
    rkvc_decoder_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.output_format = RKVC_PIX_FMT_NV12;
    cfg.threads       = 0;
    cfg.low_delay     = 0;
    return cfg;
}

/* ── 内部: 通用打开逻辑 ───────────────────────────────────────────── */

static rkvc_err decoder_open_internal(rkvc_decoder **out,
                                      const rkvc_decoder_config *cfg,
                                      const char *input_path)
{
    if (!out || !cfg)
        return RKVC_ERR_INVALID;

    rkvc_init();

    rkvc_decoder *dec = calloc(1, sizeof(*dec));
    if (!dec)
        return RKVC_ERR_NOMEM;

    dec->config = *cfg;
    dec->video_stream_idx = -1;
    pthread_mutex_init(&dec->lock, NULL);

    /*
     * 不预设 hw_device_ctx。
     * RKMPP 解码器在 avcodec_open2 时自动创建硬件设备上下文。
     */

    /* 查找解码器 */
    const AVCodec *codec = avcodec_find_decoder_by_name("hevc_rkmpp");
    if (!codec) {
        codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
        if (!codec) {
            rkvc_decoder_close(dec);
            return RKVC_ERR_NOT_FOUND;
        }
        RKVC_LOG("hevc_rkmpp not found, using software: %s", codec->name);
    }

    /* 文件模式 */
    if (input_path) {
        int ret = avformat_open_input(&dec->fmt_ctx, input_path, NULL, NULL);
        if (ret < 0) {
            rkvc_decoder_close(dec);
            return rkvc_from_averror(ret);
        }

        ret = avformat_find_stream_info(dec->fmt_ctx, NULL);
        if (ret < 0) {
            rkvc_decoder_close(dec);
            return rkvc_from_averror(ret);
        }

        /* 找视频流 */
        for (unsigned i = 0; i < dec->fmt_ctx->nb_streams; i++) {
            if (dec->fmt_ctx->streams[i]->codecpar->codec_type ==
                AVMEDIA_TYPE_VIDEO) {
                dec->video_stream_idx = (int)i;
                break;
            }
        }
        if (dec->video_stream_idx < 0) {
            rkvc_decoder_close(dec);
            return RKVC_ERR_NOT_FOUND;
        }

        /* 创建解码器上下文 */
        dec->codec_ctx = avcodec_alloc_context3(codec);
        if (!dec->codec_ctx) {
            rkvc_decoder_close(dec);
            return RKVC_ERR_NOMEM;
        }

        ret = avcodec_parameters_to_context(
            dec->codec_ctx,
            dec->fmt_ctx->streams[dec->video_stream_idx]->codecpar);
        if (ret < 0) {
            rkvc_decoder_close(dec);
            return rkvc_from_averror(ret);
        }

        dec->file_mode = 1;
    } else {
        /* 无文件模式 */
        dec->codec_ctx = avcodec_alloc_context3(codec);
        if (!dec->codec_ctx) {
            rkvc_decoder_close(dec);
            return RKVC_ERR_NOMEM;
        }
    }

    /*
     * 不手动设置 hw_device_ctx。
     * RKMPP 解码器会自动创建/复用硬件设备上下文。
     * 手动设置反而会导致 RKMPP 初始化时函数指针为 NULL 的崩溃。
     */

    /* 解码器参数 */
    if (cfg->low_delay)
        dec->codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;

    if (cfg->threads > 0)
        dec->codec_ctx->thread_count = cfg->threads;
    else
        dec->codec_ctx->thread_count = 1; /* RKMPP 单线程 */

    /* 打开解码器 */
    int ret = avcodec_open2(dec->codec_ctx, codec, NULL);
    if (ret < 0) {
        RKVC_LOG("avcodec_open2 decoder failed: %s", av_err2str(ret));
        rkvc_decoder_close(dec);
        return rkvc_from_averror(ret);
    }

    /* 分配 packet */
    dec->pkt = av_packet_alloc();
    if (!dec->pkt) {
        rkvc_decoder_close(dec);
        return RKVC_ERR_NOMEM;
    }

    *out = dec;
    return RKVC_OK;
}

rkvc_err rkvc_decoder_open(rkvc_decoder **out,
                           const rkvc_decoder_config *cfg)
{
    return decoder_open_internal(out, cfg, NULL);
}

rkvc_err rkvc_decoder_open_file(rkvc_decoder **out,
                                const rkvc_decoder_config *cfg,
                                const char *input_path)
{
    if (!input_path)
        return RKVC_ERR_INVALID;
    return decoder_open_internal(out, cfg, input_path);
}

/* ── 发送压缩包 ────────────────────────────────────────────────────── */

rkvc_err rkvc_decoder_send_packet(rkvc_decoder *dec,
                                  const uint8_t *data, int size,
                                  int64_t pts, int64_t dts)
{
    if (!dec || !dec->codec_ctx)
        return RKVC_ERR_INVALID;

    if (dec->flushed)
        return RKVC_ERR_EOF;

    if (!data || size <= 0) {
        /* flush */
        return rkvc_decoder_drain(dec);
    }

    av_packet_unref(dec->pkt);
    dec->pkt->data = (uint8_t *)data;
    dec->pkt->size = size;
    dec->pkt->pts  = pts;
    dec->pkt->dts  = dts;

    int ret = avcodec_send_packet(dec->codec_ctx, dec->pkt);
    /* 不 unref，data 是外部持有的 */

    if (ret < 0) {
        if (ret == AVERROR(EAGAIN))
            return RKVC_ERR_AGAIN;
        return rkvc_from_averror(ret);
    }

    return RKVC_OK;
}

/* ── 从文件读取下一包 ──────────────────────────────────────────────── */

rkvc_err rkvc_decoder_read_packet(rkvc_decoder *dec)
{
    if (!dec || !dec->fmt_ctx)
        return RKVC_ERR_INVALID;

    if (dec->flushed)
        return RKVC_ERR_EOF;

    av_packet_unref(dec->pkt);

    int ret;
    for (;;) {
        ret = av_read_frame(dec->fmt_ctx, dec->pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                dec->flushed = 1;
                /* 发送 flush 包 */
                avcodec_send_packet(dec->codec_ctx, NULL);
                return RKVC_ERR_EOF;
            }
            return rkvc_from_averror(ret);
        }

        /* 跳过非视频流 */
        if (dec->pkt->stream_index == dec->video_stream_idx)
            break;

        av_packet_unref(dec->pkt);
    }

    ret = avcodec_send_packet(dec->codec_ctx, dec->pkt);
    av_packet_unref(dec->pkt);

    if (ret < 0) {
        if (ret == AVERROR(EAGAIN))
            return RKVC_ERR_AGAIN;
        return rkvc_from_averror(ret);
    }

    return RKVC_OK;
}

/* ── 接收解码帧 ────────────────────────────────────────────────────── */

rkvc_err rkvc_decoder_receive_frame(rkvc_decoder *dec, rkvc_frame **f)
{
    if (!dec || !dec->codec_ctx || !f)
        return RKVC_ERR_INVALID;

    *f = NULL;

    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return RKVC_ERR_NOMEM;

    int ret = avcodec_receive_frame(dec->codec_ctx, frame);
    if (ret < 0) {
        av_frame_free(&frame);
        if (ret == AVERROR(EAGAIN))
            return RKVC_ERR_AGAIN;
        if (ret == AVERROR_EOF)
            return RKVC_ERR_EOF;
        return rkvc_from_averror(ret);
    }

    /* 如果是硬件帧，下载到软件 */
    if (frame->format == AV_PIX_FMT_RKMPP) {
        AVFrame *sw_frame = av_frame_alloc();
        if (!sw_frame) {
            av_frame_free(&frame);
            return RKVC_ERR_NOMEM;
        }

        ret = av_hwframe_transfer_data(sw_frame, frame, 0);
        av_frame_free(&frame);

        if (ret < 0) {
            av_frame_free(&sw_frame);
            return rkvc_from_averror(ret);
        }

        frame = sw_frame;
    }

    /* 包装成 rkvc_frame */
    rkvc_frame *rkf = rkvc_frame_wrap_avframe(frame);
    if (!rkf) {
        av_frame_free(&frame);
        return RKVC_ERR_NOMEM;
    }

    *f = rkf;
    return RKVC_OK;
}

/* ── Drain ─────────────────────────────────────────────────────────── */

rkvc_err rkvc_decoder_drain(rkvc_decoder *dec)
{
    if (!dec || !dec->codec_ctx)
        return RKVC_ERR_INVALID;

    if (dec->flushed)
        return RKVC_OK;

    int ret = avcodec_send_packet(dec->codec_ctx, NULL);
    if (ret < 0 && ret != AVERROR_EOF)
        return rkvc_from_averror(ret);

    dec->flushed = 1;
    return RKVC_OK;
}

/* ── 关闭 ──────────────────────────────────────────────────────────── */

rkvc_err rkvc_decoder_close(rkvc_decoder *dec)
{
    if (!dec)
        return RKVC_OK;

    if (dec->pkt)
        av_packet_free(&dec->pkt);
    if (dec->codec_ctx)
        avcodec_free_context(&dec->codec_ctx);
    if (dec->fmt_ctx)
        avformat_close_input(&dec->fmt_ctx);
    if (dec->hw_device_ctx)
        av_buffer_unref(&dec->hw_device_ctx);

    pthread_mutex_destroy(&dec->lock);
    free(dec);
    return RKVC_OK;
}

/* ── 查询接口 ──────────────────────────────────────────────────────── */

rkvc_err rkvc_decoder_get_video_info(const rkvc_decoder *dec,
                                     int *width, int *height,
                                     int *fps_num, int *fps_den)
{
    if (!dec)
        return RKVC_ERR_INVALID;

    if (dec->codec_ctx) {
        if (width)  *width  = dec->codec_ctx->width;
        if (height) *height = dec->codec_ctx->height;
    }

    if (dec->fmt_ctx && dec->video_stream_idx >= 0) {
        AVStream *st = dec->fmt_ctx->streams[dec->video_stream_idx];
        if (fps_num && fps_den) {
            AVRational r = av_guess_frame_rate(dec->fmt_ctx, st, NULL);
            *fps_num = r.num;
            *fps_den = r.den;
        }
    }

    return RKVC_OK;
}

rkvc_err rkvc_decoder_get_duration(const rkvc_decoder *dec,
                                   int64_t *duration_us)
{
    if (!dec || !duration_us)
        return RKVC_ERR_INVALID;

    if (!dec->fmt_ctx)
        return RKVC_ERR_INVALID;

    *duration_us = dec->fmt_ctx->duration;
    return RKVC_OK;
}
