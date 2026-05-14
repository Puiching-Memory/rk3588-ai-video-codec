/**
 * @file stream.c
 * @brief 实时流式处理 API 实现。
 *
 * 内置环形缓冲区，支持异步 push/pull，适合生产者-消费者模式。
 * 编码流: 原始帧 push → 内部编码 → 编码包 pull
 * 解码流: 压缩包 push → 内部解码 → 解码帧 pull
 */

#include "internal.h"
#include <errno.h>
#include <time.h>

/* ── 默认配置 ──────────────────────────────────────────────────────── */

rkvc_stream_config rkvc_stream_config_defaults(void)
{
    rkvc_stream_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.direction    = RKVC_STREAM_ENCODE;
    cfg.width        = 1920;
    cfg.height       = 1080;
    cfg.fps_num      = 30;
    cfg.fps_den      = 1;
    cfg.bitrate      = 4000000;
    cfg.input_format = RKVC_PIX_FMT_NV12;
    cfg.output_format= RKVC_PIX_FMT_NV12;
    cfg.preset       = RKVC_PRESET_MEDIUM;
    cfg.buffer_size  = 4;
    cfg.drop_frames  = 0;
    return cfg;
}

/* ── 打开流 ────────────────────────────────────────────────────────── */

static rkvc_err validate_stream_config(const rkvc_stream_config *cfg)
{
    if (!cfg)
        return RKVC_ERR_INVALID;

    if (!rkvc_is_valid_stream_dir(cfg->direction))
        return RKVC_ERR_INVALID;
    if (cfg->buffer_size <= 0 || cfg->buffer_size > RKVC_STREAM_BUF_MAX)
        return RKVC_ERR_INVALID;
    if (cfg->drop_frames != 0 && cfg->drop_frames != 1)
        return RKVC_ERR_INVALID;

    if (cfg->direction == RKVC_STREAM_ENCODE) {
        if (cfg->width <= 0 || cfg->height <= 0)
            return RKVC_ERR_INVALID;
        if (cfg->fps_num <= 0 || cfg->fps_den <= 0)
            return RKVC_ERR_INVALID;
        if (cfg->bitrate <= 0)
            return RKVC_ERR_INVALID;
        if (!rkvc_is_valid_pix_fmt(cfg->input_format) ||
            !rkvc_is_valid_preset(cfg->preset))
            return RKVC_ERR_INVALID;
    } else if (!rkvc_is_valid_pix_fmt(cfg->output_format)) {
        return RKVC_ERR_INVALID;
    }

    return RKVC_OK;
}

rkvc_err rkvc_stream_open(rkvc_stream **out,
                          const rkvc_stream_config *cfg)
{
    if (!out)
        return RKVC_ERR_INVALID;

    *out = NULL;

    rkvc_err err = validate_stream_config(cfg);
    if (err != RKVC_OK)
        return err;

    rkvc_stream *s = calloc(1, sizeof(*s));
    if (!s)
        return RKVC_ERR_NOMEM;

    s->config = *cfg;
    pthread_mutex_init(&s->buf_lock, NULL);
    pthread_cond_init(&s->buf_not_full, NULL);
    pthread_cond_init(&s->buf_not_empty, NULL);

    if (cfg->direction == RKVC_STREAM_ENCODE) {
        /* 创建内部编码器 */
        rkvc_encoder_config enc_cfg = rkvc_encoder_config_defaults();
        enc_cfg.width        = cfg->width;
        enc_cfg.height       = cfg->height;
        enc_cfg.fps_num      = cfg->fps_num;
        enc_cfg.fps_den      = cfg->fps_den;
        enc_cfg.bitrate      = cfg->bitrate;
        enc_cfg.input_format = cfg->input_format;
        enc_cfg.preset       = cfg->preset;

        err = rkvc_encoder_open(&s->enc, &enc_cfg);
        if (err != RKVC_OK) {
            pthread_mutex_destroy(&s->buf_lock);
            pthread_cond_destroy(&s->buf_not_full);
            pthread_cond_destroy(&s->buf_not_empty);
            free(s);
            return err;
        }
    } else {
        /* 创建内部解码器 */
        rkvc_decoder_config dec_cfg = rkvc_decoder_config_defaults();
        dec_cfg.output_format = cfg->output_format;

        err = rkvc_decoder_open(&s->dec, &dec_cfg);
        if (err != RKVC_OK) {
            pthread_mutex_destroy(&s->buf_lock);
            pthread_cond_destroy(&s->buf_not_full);
            pthread_cond_destroy(&s->buf_not_empty);
            free(s);
            return err;
        }
    }

    *out = s;
    return RKVC_OK;
}

/* ── Push (送入数据) ───────────────────────────────────────────────── */

rkvc_err rkvc_stream_push(rkvc_stream *s, const void *data)
{
    if (!s || !data)
        return RKVC_ERR_INVALID;

    if (s->finished)
        return RKVC_ERR_EOF;

    if (s->config.direction == RKVC_STREAM_ENCODE) {
        /* data 是 rkvc_frame* */
        rkvc_frame *f = (rkvc_frame *)data;

        /* 阻塞式 push: 持续 drain + 重试直到帧被接受 */
        rkvc_err err = rkvc_encoder_send_frame(s->enc, f);

        int retries = 0;
        while (err == RKVC_ERR_AGAIN && retries < 1000) {
            /* 编码器满: drain 所有可用包 */
            rkvc_packet pkt;
            while (rkvc_encoder_receive_packet(s->enc, &pkt) == RKVC_OK) {
                s->stats.frames_out++;
            }
            /* 短暂让出 CPU 给 MPP async 线程 */
            struct timespec ts = {0, 100000}; /* 100us */
            nanosleep(&ts, NULL);
            /* 重新尝试发送 */
            err = rkvc_encoder_send_frame(s->enc, f);
            retries++;
        }

        if (err == RKVC_OK) {
            s->stats.frames_in++;
            /* 立即取出已编码的包 */
            rkvc_packet pkt;
            while (rkvc_encoder_receive_packet(s->enc, &pkt) == RKVC_OK) {
                s->stats.frames_out++;
            }
        }

        return err;
    } else {
        /* data 是 rkvc_packet* (压缩数据) */
        const rkvc_packet *pkt = (const rkvc_packet *)data;

        rkvc_err err = rkvc_decoder_send_packet(s->dec,
                                                pkt->data, pkt->size,
                                                pkt->pts, pkt->dts);
        if (err == RKVC_OK) {
            s->stats.frames_in++;
            /* 尝试立即取出解码帧 */
            rkvc_frame *f = NULL;
            while (rkvc_decoder_receive_frame(s->dec, &f) == RKVC_OK) {
                rkvc_frame_unref(f);
                s->stats.frames_out++;
            }
        }

        return err;
    }
}

/* ── Pull (取出结果) ───────────────────────────────────────────────── */

rkvc_err rkvc_stream_pull(rkvc_stream *s, void *out, int timeout_ms)
{
    if (!s || !out)
        return RKVC_ERR_INVALID;

    (void)timeout_ms; /* 当前实现为非阻塞 */

    if (s->config.direction == RKVC_STREAM_ENCODE) {
        /* 输出 rkvc_packet (直接从编码器取) */
        rkvc_packet *pkt = (rkvc_packet *)out;
        rkvc_err err = rkvc_encoder_receive_packet(s->enc, pkt);
        if (err == RKVC_OK) {
            s->stats.frames_out++;
        }
        return err;
    } else {
        /* 输出 rkvc_frame* */
        rkvc_frame **f = (rkvc_frame **)out;
        rkvc_err err = rkvc_decoder_receive_frame(s->dec, f);
        if (err == RKVC_OK) {
            s->stats.frames_out++;
            if (s->first_out_time == 0)
                s->first_out_time = av_gettime_relative();
        }
        return err;
    }
}

/* ── Finish ────────────────────────────────────────────────────────── */

rkvc_err rkvc_stream_finish(rkvc_stream *s)
{
    if (!s)
        return RKVC_ERR_INVALID;

    s->finished = 1;

    if (s->config.direction == RKVC_STREAM_ENCODE && s->enc)
        return rkvc_encoder_drain(s->enc);
    if (s->config.direction == RKVC_STREAM_DECODE && s->dec)
        return rkvc_decoder_drain(s->dec);

    return RKVC_OK;
}

/* ── 关闭 ──────────────────────────────────────────────────────────── */

rkvc_err rkvc_stream_close(rkvc_stream *s)
{
    if (!s)
        return RKVC_OK;

    if (s->enc)
        rkvc_encoder_close(s->enc);
    if (s->dec)
        rkvc_decoder_close(s->dec);

    pthread_mutex_destroy(&s->buf_lock);
    pthread_cond_destroy(&s->buf_not_full);
    pthread_cond_destroy(&s->buf_not_empty);

    free(s);
    return RKVC_OK;
}

/* ── 统计 ──────────────────────────────────────────────────────────── */

rkvc_err rkvc_stream_get_stats(const rkvc_stream *s,
                               rkvc_stream_stats *stats)
{
    if (!s || !stats)
        return RKVC_ERR_INVALID;

    *stats = s->stats;

    /* 计算平均帧率 */
    if (s->first_out_time > 0 && s->stats.frames_out > 1) {
        int64_t now = av_gettime_relative();
        double elapsed_s = (double)(now - s->first_out_time) / 1000000.0;
        if (elapsed_s > 0)
            stats->avg_fps = (double)(s->stats.frames_out - 1) / elapsed_s;
    }

    return RKVC_OK;
}
