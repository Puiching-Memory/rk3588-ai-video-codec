/**
 * @file encoder.c
 * @brief H.265 (HEVC) RKMPP 硬件编码器实现。
 *
 * 核心流程:
 *   open  → send_frame → receive_packet → drain → close
 *
 * 文件模式下自动创建 HEVC bitstream muxer。
 * 编码器内部使用 RKMPP 硬件帧上下文实现零拷贝。
 */

#include "internal.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

/* ── 默认配置 ──────────────────────────────────────────────────────── */

rkvc_encoder_config rkvc_encoder_config_defaults(void)
{
    rkvc_encoder_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.width        = 1920;
    cfg.height       = 1080;
    cfg.fps_num      = 30;
    cfg.fps_den      = 1;
    cfg.bitrate      = 4000000;
    cfg.gop_size     = 60;
    cfg.input_format = RKVC_PIX_FMT_NV12;
    cfg.preset       = RKVC_PRESET_MEDIUM;
    cfg.rc_mode      = RKRC_CBR;
    cfg.qp           = 26;
    cfg.profile      = 0;  /* auto */
    cfg.level        = 0;  /* auto */
    cfg.num_b_frames = 0;
    cfg.threads      = 0;
    return cfg;
}

/* ── 内部: 查找对应扩展名的 muxer 短名 ─────────────────────────────── */

static const char *guess_muxer(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
        return "hevc";

    dot++;
    if (strcasecmp(dot, "mp4") == 0)  return "mp4";
    if (strcasecmp(dot, "mkv") == 0)  return "matroska";
    if (strcasecmp(dot, "ts") == 0)   return "mpegts";
    if (strcasecmp(dot, "265") == 0)  return "hevc";
    if (strcasecmp(dot, "h265") == 0) return "hevc";
    if (strcasecmp(dot, "hevc") == 0) return "hevc";
    return "hevc";
}

/* ── 内部: 配置编码器上下文 ────────────────────────────────────────── */

static rkvc_err setup_codec(AVCodecContext *ctx,
                            const rkvc_encoder_config *cfg)
{
    ctx->width     = cfg->width;
    ctx->height    = cfg->height;
    ctx->time_base = (AVRational){cfg->fps_den, cfg->fps_num};
    ctx->framerate = (AVRational){cfg->fps_num, cfg->fps_den};
    ctx->bit_rate  = cfg->bitrate;
    ctx->gop_size  = cfg->gop_size;

    /* sw_pix_fmt 用于软件帧输入; pix_fmt 由 hw_frames_ctx 决定 */
    ctx->sw_pix_fmt = rkvc_to_av_pix_fmt(cfg->input_format);
    if (ctx->hw_frames_ctx)
        ctx->pix_fmt = AV_PIX_FMT_RKMPP;
    else
        ctx->pix_fmt = ctx->sw_pix_fmt;

    ctx->max_b_frames = cfg->num_b_frames;

    if (cfg->profile > 0)
        ctx->profile = cfg->profile;
    if (cfg->level > 0)
        ctx->level = cfg->level;
    if (cfg->threads > 0)
        ctx->thread_count = cfg->threads;

    /* 编码器私有选项 (通过 AVOption 设置, 忽略不支持的选项) */
    switch (cfg->preset) {
    case RKVC_PRESET_FAST:
        av_opt_set(ctx->priv_data, "preset", "fast", 0);
        break;
    case RKVC_PRESET_SLOW:
        av_opt_set(ctx->priv_data, "preset", "slow", 0);
        break;
    default:
        av_opt_set(ctx->priv_data, "preset", "medium", 0);
        break;
    }

    switch (cfg->rc_mode) {
    case RKRC_VBR:
        av_opt_set(ctx->priv_data, "rc_mode", "VBR", 0);
        break;
    case RKRC_CQP:
        av_opt_set(ctx->priv_data, "rc_mode", "CQP", 0);
        ctx->global_quality = cfg->qp;
        break;
    default:
        av_opt_set(ctx->priv_data, "rc_mode", "CBR", 0);
        break;
    }

    return RKVC_OK;
}

/* ── 内部: 创建硬件帧上下文 ───────────────────────────────────────── */

static rkvc_err create_hw_frames_ctx(AVCodecContext *ctx,
                                     AVBufferRef *hw_device_ctx,
                                     int width, int height,
                                     enum AVPixelFormat sw_fmt)
{
    AVBufferRef *hw_frames_ref = NULL;
    AVHWFramesContext *hw_frames_ctx;
    int ret;

    /*
     * RKMPP 编码器使用 DRM prime 帧。
     * 创建硬件帧上下文，sw_format 设为源像素格式 (通常 NV12)。
     * 编码器内部完成 MPP buffer 转换。
     */
    hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
    if (!hw_frames_ref)
        return RKVC_ERR_NOMEM;

    hw_frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
    hw_frames_ctx->format    = AV_PIX_FMT_RKMPP;
    hw_frames_ctx->sw_format = sw_fmt;
    hw_frames_ctx->width     = width;
    hw_frames_ctx->height    = height;
    hw_frames_ctx->initial_pool_size = 32;

    ret = av_hwframe_ctx_init(hw_frames_ref);
    if (ret < 0) {
        av_buffer_unref(&hw_frames_ref);
        RKVC_LOG("hw frame ctx init failed: %s", av_err2str(ret));
        return rkvc_from_averror(ret);
    }

    ctx->hw_frames_ctx = hw_frames_ref;
    ctx->pix_fmt = AV_PIX_FMT_RKMPP;

    return RKVC_OK;
}

/* ── 编码器打开 ────────────────────────────────────────────────────── */

static rkvc_err encoder_open_internal(rkvc_encoder **out,
                                      const rkvc_encoder_config *cfg,
                                      const char *output_path)
{
    if (!out || !cfg)
        return RKVC_ERR_INVALID;
    if (cfg->width <= 0 || cfg->height <= 0)
        return RKVC_ERR_INVALID;

    rkvc_init();

    rkvc_encoder *enc = calloc(1, sizeof(*enc));
    if (!enc)
        return RKVC_ERR_NOMEM;

    enc->config = *cfg;
    pthread_mutex_init(&enc->lock, NULL);

    /* 获取硬件设备上下文 */
    rkvc_err err = rkvc_get_hw_device_ctx(&enc->hw_device_ctx);
    if (err != RKVC_OK) {
        RKVC_LOG("hw device ctx failed, falling back to software");
        enc->hw_device_ctx = NULL;
    }

    /* 查找编码器 */
    const AVCodec *codec = avcodec_find_encoder_by_name("hevc_rkmpp");
    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
        if (!codec) {
            rkvc_encoder_close(enc);
            return RKVC_ERR_NOT_FOUND;
        }
        RKVC_LOG("hevc_rkmpp not found, using software: %s", codec->name);
    }

    /* 创建编码器上下文 */
    enc->codec_ctx = avcodec_alloc_context3(codec);
    if (!enc->codec_ctx) {
        rkvc_encoder_close(enc);
        return RKVC_ERR_NOMEM;
    }

    /* 创建硬件帧上下文 (如果有硬件设备) */
    if (enc->hw_device_ctx) {
        err = create_hw_frames_ctx(enc->codec_ctx, enc->hw_device_ctx,
                                   cfg->width, cfg->height,
                                   rkvc_to_av_pix_fmt(cfg->input_format));
        if (err != RKVC_OK) {
            RKVC_LOG("hw frames ctx failed, software encode only");
            av_buffer_unref(&enc->hw_device_ctx);
            enc->hw_device_ctx = NULL;
        }
    }

    /* 配置编码器参数 */
    err = setup_codec(enc->codec_ctx, cfg);
    if (err != RKVC_OK) {
        rkvc_encoder_close(enc);
        return err;
    }

    /* 打开编码器 */
    int ret = avcodec_open2(enc->codec_ctx, codec, NULL);
    if (ret < 0) {
        RKVC_LOG("avcodec_open2 failed: %s", av_err2str(ret));
        rkvc_encoder_close(enc);
        return rkvc_from_averror(ret);
    }

    /* 分配 packet */
    enc->pkt = av_packet_alloc();
    if (!enc->pkt) {
        rkvc_encoder_close(enc);
        return RKVC_ERR_NOMEM;
    }

    /* 文件模式: 创建 muxer */
    if (output_path) {
        const char *muxer_name = guess_muxer(output_path);
        ret = avformat_alloc_output_context2(&enc->fmt_ctx, NULL,
                                             muxer_name, output_path);
        if (ret < 0) {
            rkvc_encoder_close(enc);
            return rkvc_from_averror(ret);
        }

        enc->av_stream = avformat_new_stream(enc->fmt_ctx, NULL);
        if (!enc->av_stream) {
            rkvc_encoder_close(enc);
            return RKVC_ERR_MUX;
        }

        /* 从编码器复制参数到流 */
        ret = avcodec_parameters_from_context(enc->av_stream->codecpar,
                                              enc->codec_ctx);
        if (ret < 0) {
            rkvc_encoder_close(enc);
            return rkvc_from_averror(ret);
        }
        enc->av_stream->time_base = enc->codec_ctx->time_base;

        /* 打开输出文件 */
        if (!(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open(&enc->fmt_ctx->pb, output_path, AVIO_FLAG_WRITE);
            if (ret < 0) {
                rkvc_encoder_close(enc);
                return rkvc_from_averror(ret);
            }
        }

        /* 写文件头 */
        ret = avformat_write_header(enc->fmt_ctx, NULL);
        if (ret < 0) {
            rkvc_encoder_close(enc);
            return rkvc_from_averror(ret);
        }

        enc->file_mode = 1;
    }

    *out = enc;
    return RKVC_OK;
}

rkvc_err rkvc_encoder_open(rkvc_encoder **out,
                           const rkvc_encoder_config *cfg)
{
    return encoder_open_internal(out, cfg, NULL);
}

rkvc_err rkvc_encoder_open_file(rkvc_encoder **out,
                                const rkvc_encoder_config *cfg,
                                const char *output_path)
{
    if (!output_path)
        return RKVC_ERR_INVALID;
    return encoder_open_internal(out, cfg, output_path);
}

/* ── 发送帧 ────────────────────────────────────────────────────────── */

rkvc_err rkvc_encoder_send_frame(rkvc_encoder *enc, rkvc_frame *f)
{
    if (!enc || !enc->codec_ctx)
        return RKVC_ERR_INVALID;

    if (enc->flushed)
        return RKVC_ERR_EOF;

    AVFrame *frame = NULL;
    if (f) {
        frame = f->av_frame;
        /*
         * RKMPP 编码器要求 DMA 硬件帧 (linear / AFBC)。
         * 将软件 NV12 帧上传到硬件表面再送入编码器。
         */
        if (enc->codec_ctx->hw_frames_ctx &&
            frame->format != AV_PIX_FMT_RKMPP &&
            frame->format != AV_PIX_FMT_DRM_PRIME) {
            AVFrame *hw_frame = av_frame_alloc();
            if (!hw_frame)
                return RKVC_ERR_NOMEM;

            hw_frame->format = AV_PIX_FMT_RKMPP;
            int ret = av_hwframe_get_buffer(enc->codec_ctx->hw_frames_ctx,
                                            hw_frame, 0);
            if (ret < 0) {
                av_frame_free(&hw_frame);
                return rkvc_from_averror(ret);
            }

            ret = av_hwframe_transfer_data(hw_frame, frame, 0);
            if (ret < 0) {
                av_frame_free(&hw_frame);
                return rkvc_from_averror(ret);
            }

            hw_frame->pts = frame->pts;
            frame = hw_frame;
        }
    }

    int ret = avcodec_send_frame(enc->codec_ctx, frame);

    /* 编码器内部会引用 hw_frame，此处释放我们的引用 */
    if (f && frame != f->av_frame)
        av_frame_free(&frame);

    if (ret < 0) {
        if (ret == AVERROR(EAGAIN))
            return RKVC_ERR_AGAIN;
        return rkvc_from_averror(ret);
    }

    return RKVC_OK;
}

rkvc_err rkvc_encoder_send_buffer(rkvc_encoder *enc,
                                  const uint8_t *data, int linesize,
                                  int64_t pts)
{
    if (!enc || !data)
        return RKVC_ERR_INVALID;

    rkvc_frame *f = NULL;
    rkvc_err err = rkvc_frame_alloc(&f, enc->config.width,
                                    enc->config.height,
                                    enc->config.input_format);
    if (err != RKVC_OK)
        return err;

    /* 复制像素数据 */
    AVFrame *dst = f->av_frame;
    const uint8_t *src_data[4] = {NULL, NULL, NULL, NULL};
    int src_linesize[4] = {0, 0, 0, 0};

    enum AVPixelFormat fmt = rkvc_to_av_pix_fmt(enc->config.input_format);
    int num_planes = 1;
    if (fmt == AV_PIX_FMT_NV12 || fmt == AV_PIX_FMT_P010)
        num_planes = 2;
    else if (fmt == AV_PIX_FMT_YUV420P)
        num_planes = 3;

    src_data[0] = data;
    src_linesize[0] = linesize;

    /* 估算其他平面 */
    if (num_planes >= 2) {
        src_data[1] = data + linesize * enc->config.height;
        src_linesize[1] = linesize;
    }
    if (num_planes >= 3) {
        src_data[2] = data + linesize * enc->config.height * 5 / 4;
        src_linesize[2] = linesize / 2;
    }

    for (int i = 0; i < num_planes; i++) {
        if (dst->data[i]) {
            int h = enc->config.height;
            if (i >= 1 && fmt == AV_PIX_FMT_YUV420P)
                h /= 2;
            for (int row = 0; row < h; row++) {
                memcpy(dst->data[i] + row * dst->linesize[i],
                       src_data[i] + row * src_linesize[i],
                       src_linesize[i]);
            }
        }
    }

    dst->pts = pts;
    f->info.pts = pts;

    err = rkvc_encoder_send_frame(enc, f);
    rkvc_frame_unref(f);
    return err;
}

/* ── 接收包 ────────────────────────────────────────────────────────── */

rkvc_err rkvc_encoder_receive_packet(rkvc_encoder *enc, rkvc_packet *pkt)
{
    if (!enc || !enc->codec_ctx || !pkt)
        return RKVC_ERR_INVALID;

    memset(pkt, 0, sizeof(*pkt));

    int ret = avcodec_receive_packet(enc->codec_ctx, enc->pkt);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN))
            return RKVC_ERR_AGAIN;
        if (ret == AVERROR_EOF)
            return RKVC_ERR_EOF;
        return rkvc_from_averror(ret);
    }

    /* 文件模式: 写入封装 */
    if (enc->file_mode && enc->fmt_ctx) {
        av_packet_rescale_ts(enc->pkt, enc->codec_ctx->time_base,
                             enc->av_stream->time_base);
        enc->pkt->stream_index = enc->av_stream->index;

        ret = av_interleaved_write_frame(enc->fmt_ctx, enc->pkt);
        if (ret < 0) {
            av_packet_unref(enc->pkt);
            return RKVC_ERR_MUX;
        }
    }

    /* 提取元数据到输出结构 */
    pkt->data      = enc->pkt->data;
    pkt->size      = enc->pkt->size;
    pkt->pts       = enc->pkt->pts;
    pkt->dts       = enc->pkt->dts;
    pkt->key_frame = (enc->pkt->flags & AV_PKT_FLAG_KEY) ? 1 : 0;
    pkt->pos       = enc->pkt->pos;

    if (enc->file_mode) {
        /* 文件模式: av_interleaved_write_frame 已消费数据，清理 */
        av_packet_unref(enc->pkt);
    }
    /* 非文件模式: enc->pkt 保持有效，数据在下次 send_frame/receive 前可用 */

    return RKVC_OK;
}

/* ── Drain ─────────────────────────────────────────────────────────── */

rkvc_err rkvc_encoder_drain(rkvc_encoder *enc)
{
    if (!enc || !enc->codec_ctx)
        return RKVC_ERR_INVALID;

    if (enc->flushed)
        return RKVC_OK;

    /* 发送 NULL 帧触发 flush */
    int ret = avcodec_send_frame(enc->codec_ctx, NULL);
    if (ret < 0 && ret != AVERROR_EOF)
        return rkvc_from_averror(ret);

    enc->flushed = 1;
    return RKVC_OK;
}

/* ── 关闭 ──────────────────────────────────────────────────────────── */

rkvc_err rkvc_encoder_close(rkvc_encoder *enc)
{
    if (!enc)
        return RKVC_OK;

    /* 自动 drain (如果尚未 drain 且有待处理的帧) */
    if (enc->codec_ctx && !enc->flushed) {
        rkvc_encoder_drain(enc);

        /* flush 残余包 */
        while (avcodec_receive_packet(enc->codec_ctx, enc->pkt) >= 0) {
            if (enc->file_mode && enc->fmt_ctx) {
                av_packet_rescale_ts(enc->pkt, enc->codec_ctx->time_base,
                                     enc->av_stream->time_base);
                enc->pkt->stream_index = enc->av_stream->index;
                av_interleaved_write_frame(enc->fmt_ctx, enc->pkt);
            }
            av_packet_unref(enc->pkt);
        }
    }

    /* 文件模式: 写 trailer */
    if (enc->file_mode && enc->fmt_ctx) {
        av_write_trailer(enc->fmt_ctx);
    }

    /* 释放资源 */
    if (enc->pkt)
        av_packet_free(&enc->pkt);
    if (enc->codec_ctx) {
        avcodec_free_context(&enc->codec_ctx);
    }
    if (enc->fmt_ctx) {
        if (!(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE) && enc->fmt_ctx->pb)
            avio_closep(&enc->fmt_ctx->pb);
        avformat_free_context(enc->fmt_ctx);
    }
    if (enc->hw_device_ctx)
        av_buffer_unref(&enc->hw_device_ctx);

    pthread_mutex_destroy(&enc->lock);
    free(enc);
    return RKVC_OK;
}

/* ── 查询接口 ──────────────────────────────────────────────────────── */

rkvc_err rkvc_encoder_timebase(const rkvc_encoder *enc, int *num, int *den)
{
    if (!enc || !enc->codec_ctx || !num || !den)
        return RKVC_ERR_INVALID;

    *num = enc->codec_ctx->time_base.num;
    *den = enc->codec_ctx->time_base.den;
    return RKVC_OK;
}

rkvc_err rkvc_encoder_get_config(const rkvc_encoder *enc,
                                 rkvc_encoder_config *cfg)
{
    if (!enc || !cfg)
        return RKVC_ERR_INVALID;

    *cfg = enc->config;
    return RKVC_OK;
}
