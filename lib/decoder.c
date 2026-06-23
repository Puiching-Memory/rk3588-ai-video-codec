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

static rkvc_err validate_decoder_config(const rkvc_decoder_config *cfg)
{
    if (!cfg)
        return RKVC_ERR_INVALID;

    if (!rkvc_is_valid_pix_fmt(cfg->output_format))
        return RKVC_ERR_INVALID;
    if (cfg->threads < 0)
        return RKVC_ERR_INVALID;
    if (cfg->low_delay != 0 && cfg->low_delay != 1)
        return RKVC_ERR_INVALID;

    return RKVC_OK;
}

static rkvc_err decoder_open_internal(rkvc_decoder **out,
                                      const rkvc_decoder_config *cfg,
                                      const char *input_path)
{
    if (!out)
        return RKVC_ERR_INVALID;

    *out = NULL;

    rkvc_err err = validate_decoder_config(cfg);
    if (err != RKVC_OK)
        return err;

    rkvc_init();

    rkvc_decoder *dec = rkvc_calloc(1, sizeof(*dec));
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
    int using_rkmpp = (codec != NULL);
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

    if (using_rkmpp) {
        err = rkvc_check_hw_permissions();
        if (err != RKVC_OK) {
            rkvc_decoder_close(dec);
            return err;
        }
    }

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
    if (!input_path || input_path[0] == '\0')
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

    /* 处理硬件帧下载与输出格式转换。
     *
     * RKMPP 解码器有两种交付路径:
     *   1. AV_PIX_FMT_DRM_PRIME  —— 交付硬件帧, 需 av_hwframe_transfer_data
     *      下载到软件。
     *   2. 其他 (NV12/NV16/NV24/...) —— 解码器内部已完成 transfer, 直接
     *      交付软件帧 (RKMPP 在 ff_get_format 选择了软件 pix_fmt)。
     *
     * RKMPP 硬件帧池只支持与输入码流匹配的有限格式集 (8-bit HEVC→NV12,
     * 10-bit→NV15, 4:2:2 仅 H.264→NV16 等)。若用户请求的 output_format
     * 硬件无法直接提供, 历史上会静默回退到池默认格式 (通常 NV12), 造成
     * "配置失效"的隐蔽 bug。
     *
     * 修复策略: 先尝试让硬件直接输出请求格式 (设置 sw_frame->format 后
     * transfer); 若硬件不支持或静默回退到其它格式, 则用 libswscale 把
     * 实际下载到的软件帧软转换为请求格式。最终交付的帧格式必须等于
     * cfg.output_format, 否则返回 RKVC_ERR_FORMAT (绝不静默错配)。
     */
    enum AVPixelFormat requested_av =
        rkvc_to_av_pix_fmt(dec->config.output_format);

    if (frame->format == AV_PIX_FMT_RKMPP) {
        AVFrame *sw_frame = av_frame_alloc();
        if (!sw_frame) {
            av_frame_free(&frame);
            return RKVC_ERR_NOMEM;
        }

        sw_frame->format = requested_av;
        ret = av_hwframe_transfer_data(sw_frame, frame, 0);

        if (ret < 0) {
            /* 请求格式硬件帧池不支持 —— 下载到默认格式后走 sws_scale。 */
            av_frame_free(&sw_frame);
            sw_frame = av_frame_alloc();
            if (!sw_frame) {
                av_frame_free(&frame);
                return RKVC_ERR_NOMEM;
            }
            ret = av_hwframe_transfer_data(sw_frame, frame, 0);
        }
        av_frame_free(&frame);

        if (ret < 0) {
            av_frame_free(&sw_frame);
            return rkvc_from_averror(ret);
        }
        frame = sw_frame;
    }

    /* 此时 frame 为软件帧, 但格式可能不等于请求格式 (硬件回退或解码器
     * 直接交付了 NV12)。用 libswscale 做格式转换。
     * 优化: 复用缓存的 SwsContext, 避免每帧 sws_getContext/freeContext
     * (这是纯 CPU 软转换的性能关键路径, 上下文创建开销很大)。 */
    if (frame->format != requested_av) {
        /* 检查是否可复用缓存的 sws 上下文 (源格式/尺寸/目标格式不变) */
        if (dec->sws_cache &&
            (dec->sws_src_w != frame->width ||
             dec->sws_src_h != frame->height ||
             dec->sws_src_fmt != frame->format ||
             dec->sws_dst_fmt != requested_av)) {
            sws_freeContext(dec->sws_cache);
            dec->sws_cache = NULL;
        }

        if (!dec->sws_cache) {
            dec->sws_cache = sws_getContext(
                frame->width, frame->height, frame->format,
                frame->width, frame->height, requested_av,
                SWS_BILINEAR, NULL, NULL, NULL);
            if (!dec->sws_cache) {
                av_frame_free(&frame);
                return RKVC_ERR_FORMAT;
            }
            dec->sws_src_w   = frame->width;
            dec->sws_src_h   = frame->height;
            dec->sws_src_fmt = frame->format;
            dec->sws_dst_fmt = requested_av;
        }

        AVFrame *dst = av_frame_alloc();
        if (!dst) {
            av_frame_free(&frame);
            return RKVC_ERR_NOMEM;
        }
        dst->width  = frame->width;
        dst->height = frame->height;
        dst->format = requested_av;
        dst->pts    = frame->pts;
        dst->flags  = frame->flags;

        ret = av_frame_get_buffer(dst, 0);
        if (ret < 0) {
            av_frame_free(&dst);
            av_frame_free(&frame);
            return rkvc_from_averror(ret);
        }

        ret = sws_scale(dec->sws_cache,
                        (const uint8_t *const *)frame->data,
                        frame->linesize, 0, frame->height,
                        dst->data, dst->linesize);
        av_frame_free(&frame);

        if (ret <= 0) {
            av_frame_free(&dst);
            return RKVC_ERR_FORMAT;
        }
        frame = dst;
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

    if (dec->sws_cache) {
        sws_freeContext(dec->sws_cache);
        dec->sws_cache = NULL;
    }
    if (dec->pkt)
        av_packet_free(&dec->pkt);
    if (dec->codec_ctx)
        avcodec_free_context(&dec->codec_ctx);
    if (dec->fmt_ctx)
        avformat_close_input(&dec->fmt_ctx);
    if (dec->hw_device_ctx)
        av_buffer_unref(&dec->hw_device_ctx);

    pthread_mutex_destroy(&dec->lock);
    rkvc_free(dec);
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
