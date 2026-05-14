/**
 * @file test_internal.c
 * @brief Internal mapping and conversion tests.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <errno.h>
#include <string.h>
#include <cmocka.h>

#include "internal.h"

static void test_averror_mapping(void **state)
{
    (void)state;

    assert_int_equal(rkvc_from_averror(0), RKVC_OK);
    assert_int_equal(rkvc_from_averror(1), RKVC_OK);
    assert_int_equal(rkvc_from_averror(AVERROR(ENOMEM)), RKVC_ERR_NOMEM);
    assert_int_equal(rkvc_from_averror(AVERROR(EINVAL)), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_from_averror(AVERROR(ENOENT)), RKVC_ERR_NOT_FOUND);
    assert_int_equal(rkvc_from_averror(AVERROR(EIO)), RKVC_ERR_IO);
    assert_int_equal(rkvc_from_averror(AVERROR(ENODEV)), RKVC_ERR_HW);
    assert_int_equal(rkvc_from_averror(AVERROR(EAGAIN)), RKVC_ERR_AGAIN);
    assert_int_equal(rkvc_from_averror(AVERROR_EOF), RKVC_ERR_EOF);
    assert_int_equal(rkvc_from_averror(AVERROR_DECODER_NOT_FOUND),
                     RKVC_ERR_NOT_FOUND);
    assert_int_equal(rkvc_from_averror(AVERROR_ENCODER_NOT_FOUND),
                     RKVC_ERR_NOT_FOUND);
    assert_int_equal(rkvc_from_averror(AVERROR_BUG), RKVC_ERR_INTERNAL);
}

static void test_enum_validators(void **state)
{
    (void)state;

    assert_true(rkvc_is_valid_pix_fmt(RKVC_PIX_FMT_NV12));
    assert_true(rkvc_is_valid_pix_fmt(RKVC_PIX_FMT_YUV420P));
    assert_true(rkvc_is_valid_pix_fmt(RKVC_PIX_FMT_NV16));
    assert_true(rkvc_is_valid_pix_fmt(RKVC_PIX_FMT_P010));
    assert_false(rkvc_is_valid_pix_fmt((rkvc_pix_fmt)-1));
    assert_false(rkvc_is_valid_pix_fmt((rkvc_pix_fmt)99));

    assert_true(rkvc_is_valid_preset(RKVC_PRESET_FAST));
    assert_true(rkvc_is_valid_preset(RKVC_PRESET_MEDIUM));
    assert_true(rkvc_is_valid_preset(RKVC_PRESET_SLOW));
    assert_false(rkvc_is_valid_preset((rkvc_preset)99));

    assert_true(rkvc_is_valid_rc_mode(RKRC_CBR));
    assert_true(rkvc_is_valid_rc_mode(RKRC_VBR));
    assert_true(rkvc_is_valid_rc_mode(RKRC_CQP));
    assert_false(rkvc_is_valid_rc_mode((rkvc_rc_mode)99));

    assert_true(rkvc_is_valid_stream_dir(RKVC_STREAM_ENCODE));
    assert_true(rkvc_is_valid_stream_dir(RKVC_STREAM_DECODE));
    assert_false(rkvc_is_valid_stream_dir((rkvc_stream_dir)99));
}

static void test_pixel_format_round_trip(void **state)
{
    (void)state;

    assert_int_equal(rkvc_to_av_pix_fmt(RKVC_PIX_FMT_NV12), AV_PIX_FMT_NV12);
    assert_int_equal(rkvc_to_av_pix_fmt(RKVC_PIX_FMT_YUV420P),
                     AV_PIX_FMT_YUV420P);
    assert_int_equal(rkvc_to_av_pix_fmt(RKVC_PIX_FMT_NV16), AV_PIX_FMT_NV16);
    assert_int_equal(rkvc_to_av_pix_fmt(RKVC_PIX_FMT_P010), AV_PIX_FMT_P010);
    assert_int_equal(rkvc_to_av_pix_fmt((rkvc_pix_fmt)99), AV_PIX_FMT_NONE);

    assert_int_equal(rkvc_from_av_pix_fmt(AV_PIX_FMT_NV12),
                     RKVC_PIX_FMT_NV12);
    assert_int_equal(rkvc_from_av_pix_fmt(AV_PIX_FMT_YUV420P),
                     RKVC_PIX_FMT_YUV420P);
    assert_int_equal(rkvc_from_av_pix_fmt(AV_PIX_FMT_NV16),
                     RKVC_PIX_FMT_NV16);
    assert_int_equal(rkvc_from_av_pix_fmt(AV_PIX_FMT_P010),
                     RKVC_PIX_FMT_P010);
    assert_int_equal(rkvc_from_av_pix_fmt(AV_PIX_FMT_NONE),
                     RKVC_PIX_FMT_NV12);
}

static void test_frame_wrap_contracts(void **state)
{
    (void)state;

    assert_null(rkvc_frame_wrap_avframe(NULL));

    AVFrame *av_frame = av_frame_alloc();
    assert_non_null(av_frame);
    av_frame->width = 32;
    av_frame->height = 16;
    av_frame->format = AV_PIX_FMT_NV12;
    av_frame->pts = 77;
    av_frame->flags |= AV_FRAME_FLAG_KEY;

    rkvc_frame *frame = rkvc_frame_wrap_avframe(av_frame);
    assert_non_null(frame);

    rkvc_frame_info info;
    assert_int_equal(rkvc_frame_get_info(frame, &info), RKVC_OK);
    assert_int_equal(info.width, 32);
    assert_int_equal(info.height, 16);
    assert_int_equal(info.format, RKVC_PIX_FMT_NV12);
    assert_int_equal(info.pts, 77);
    assert_int_equal(info.key_frame, 1);

    rkvc_frame_unref(frame);
}

static rkvc_encoder *make_fake_encoder(rkvc_pix_fmt format)
{
    rkvc_encoder *encoder = rkvc_calloc(1, sizeof(*encoder));
    assert_non_null(encoder);

    encoder->config = rkvc_encoder_config_defaults();
    encoder->config.width = 8;
    encoder->config.height = 4;
    encoder->config.input_format = format;
    encoder->codec_ctx = avcodec_alloc_context3(NULL);
    assert_non_null(encoder->codec_ctx);
    encoder->codec_ctx->time_base = (AVRational){1, 30};
    encoder->pkt = av_packet_alloc();
    assert_non_null(encoder->pkt);
    encoder->flushed = 1;
    pthread_mutex_init(&encoder->lock, NULL);

    return encoder;
}

static void test_encoder_fake_getters_and_flushed_paths(void **state)
{
    (void)state;

    rkvc_encoder *encoder = make_fake_encoder(RKVC_PIX_FMT_NV12);
    int num = 0;
    int den = 0;
    rkvc_encoder_config cfg;
    rkvc_frame *frame = NULL;

    assert_int_equal(rkvc_encoder_timebase(encoder, &num, &den), RKVC_OK);
    assert_int_equal(num, 1);
    assert_int_equal(den, 30);

    assert_int_equal(rkvc_encoder_get_config(encoder, &cfg), RKVC_OK);
    assert_int_equal(cfg.width, 8);
    assert_int_equal(cfg.height, 4);

    assert_int_equal(rkvc_frame_alloc(&frame, 8, 4, RKVC_PIX_FMT_NV12),
                     RKVC_OK);
    assert_int_equal(rkvc_encoder_send_frame(encoder, frame), RKVC_ERR_EOF);
    rkvc_frame_unref(frame);

    assert_int_equal(rkvc_encoder_drain(encoder), RKVC_OK);
    assert_int_equal(rkvc_encoder_close(encoder), RKVC_OK);
}

#ifdef RKVC_ENABLE_FAULT_INJECTION
static void test_encoder_pure_helper_paths(void **state)
{
    (void)state;

    assert_string_equal(rkvc_test_guess_muxer("out"), "hevc");
    assert_string_equal(rkvc_test_guess_muxer("out.mp4"), "mp4");
    assert_string_equal(rkvc_test_guess_muxer("out.MKV"), "matroska");
    assert_string_equal(rkvc_test_guess_muxer("out.ts"), "mpegts");
    assert_string_equal(rkvc_test_guess_muxer("out.265"), "hevc");
    assert_string_equal(rkvc_test_guess_muxer("out.h265"), "hevc");
    assert_string_equal(rkvc_test_guess_muxer("out.hevc"), "hevc");
    assert_string_equal(rkvc_test_guess_muxer("out.unknown"), "hevc");

    struct {
        rkvc_preset preset;
        rkvc_rc_mode rc_mode;
        int qp;
    } cases[] = {
        {RKVC_PRESET_FAST, RKRC_VBR, 26},
        {RKVC_PRESET_SLOW, RKRC_CQP, 31},
        {RKVC_PRESET_MEDIUM, RKRC_CBR, 26},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        AVCodecContext *ctx = avcodec_alloc_context3(NULL);
        rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
        assert_non_null(ctx);

        cfg.width = 640;
        cfg.height = 360;
        cfg.fps_num = 25;
        cfg.fps_den = 1;
        cfg.bitrate = 1000000;
        cfg.gop_size = 25;
        cfg.preset = cases[i].preset;
        cfg.rc_mode = cases[i].rc_mode;
        cfg.qp = cases[i].qp;
        cfg.profile = 1;
        cfg.level = 120;
        cfg.threads = 2;

        assert_int_equal(rkvc_test_setup_encoder_codec(ctx, &cfg), RKVC_OK);
        assert_int_equal(ctx->width, cfg.width);
        assert_int_equal(ctx->height, cfg.height);
        assert_int_equal(ctx->time_base.num, cfg.fps_den);
        assert_int_equal(ctx->time_base.den, cfg.fps_num);
        assert_int_equal(ctx->framerate.num, cfg.fps_num);
        assert_int_equal(ctx->framerate.den, cfg.fps_den);
        assert_int_equal(ctx->bit_rate, cfg.bitrate);
        assert_int_equal(ctx->gop_size, cfg.gop_size);
        assert_int_equal(ctx->sw_pix_fmt, AV_PIX_FMT_NV12);
        assert_int_equal(ctx->pix_fmt, AV_PIX_FMT_NV12);
        assert_int_equal(ctx->profile, cfg.profile);
        assert_int_equal(ctx->level, cfg.level);
        assert_int_equal(ctx->thread_count, cfg.threads);
        if (cfg.rc_mode == RKRC_CQP)
            assert_int_equal(ctx->global_quality, cfg.qp);

        avcodec_free_context(&ctx);
    }
}
#endif

static void test_encoder_send_buffer_layouts(void **state)
{
    (void)state;

    struct {
        rkvc_pix_fmt format;
        int linesize;
        int size;
    } cases[] = {
        {RKVC_PIX_FMT_NV12, 8, 48},
        {RKVC_PIX_FMT_YUV420P, 8, 48},
        {RKVC_PIX_FMT_NV16, 8, 64},
        {RKVC_PIX_FMT_P010, 16, 96},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t data[128];
        rkvc_encoder *encoder = make_fake_encoder(cases[i].format);

        assert_true(cases[i].size <= (int)sizeof(data));
        memset(data, (int)(0x20 + i), (size_t)cases[i].size);

        assert_int_equal(rkvc_encoder_send_buffer(encoder, data,
                                                  cases[i].linesize, 123),
                         RKVC_ERR_EOF);
        assert_int_equal(rkvc_encoder_close(encoder), RKVC_OK);
    }
}

static void test_encoder_send_buffer_rejects_bad_linesize(void **state)
{
    (void)state;

    uint8_t data[128] = {0};
    rkvc_encoder *encoder = make_fake_encoder(RKVC_PIX_FMT_NV12);

    assert_int_equal(rkvc_encoder_send_buffer(encoder, data, 0, 0),
                     RKVC_ERR_INVALID);
    assert_int_equal(rkvc_encoder_send_buffer(encoder, data, 7, 0),
                     RKVC_ERR_INVALID);
    assert_int_equal(rkvc_encoder_close(encoder), RKVC_OK);

    encoder = make_fake_encoder(RKVC_PIX_FMT_P010);
    assert_int_equal(rkvc_encoder_send_buffer(encoder, data, 15, 0),
                     RKVC_ERR_INVALID);
    assert_int_equal(rkvc_encoder_close(encoder), RKVC_OK);
}

static void test_encoder_fake_active_error_paths(void **state)
{
    (void)state;

    rkvc_encoder *encoder = make_fake_encoder(RKVC_PIX_FMT_NV12);
    rkvc_packet pkt;
    rkvc_frame *frame = NULL;

    encoder->flushed = 0;
    assert_int_equal(rkvc_frame_alloc(&frame, 8, 4, RKVC_PIX_FMT_NV12),
                     RKVC_OK);
    assert_int_not_equal(rkvc_encoder_send_frame(encoder, frame), RKVC_OK);
    rkvc_frame_unref(frame);

    assert_int_not_equal(rkvc_encoder_receive_packet(encoder, &pkt), RKVC_OK);
    assert_int_not_equal(rkvc_encoder_drain(encoder), RKVC_OK);

    encoder->flushed = 1;
    assert_int_equal(rkvc_encoder_close(encoder), RKVC_OK);
}

static rkvc_decoder *make_fake_decoder(void)
{
    rkvc_decoder *decoder = rkvc_calloc(1, sizeof(*decoder));
    assert_non_null(decoder);

    decoder->config = rkvc_decoder_config_defaults();
    decoder->video_stream_idx = -1;
    decoder->codec_ctx = avcodec_alloc_context3(NULL);
    assert_non_null(decoder->codec_ctx);
    decoder->codec_ctx->width = 320;
    decoder->codec_ctx->height = 240;
    decoder->pkt = av_packet_alloc();
    assert_non_null(decoder->pkt);
    decoder->flushed = 1;
    pthread_mutex_init(&decoder->lock, NULL);

    return decoder;
}

static void test_decoder_fake_getters_and_flushed_paths(void **state)
{
    (void)state;

    rkvc_decoder *decoder = make_fake_decoder();
    uint8_t byte = 0;
    int width = 0;
    int height = 0;
    int64_t duration = 0;

    assert_int_equal(rkvc_decoder_send_packet(decoder, &byte, 1, 2, 1),
                     RKVC_ERR_EOF);
    assert_int_equal(rkvc_decoder_read_packet(decoder), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_decoder_drain(decoder), RKVC_OK);
    assert_int_equal(rkvc_decoder_get_video_info(decoder, &width, &height,
                                                 NULL, NULL),
                     RKVC_OK);
    assert_int_equal(width, 320);
    assert_int_equal(height, 240);
    assert_int_equal(rkvc_decoder_get_duration(decoder, &duration),
                     RKVC_ERR_INVALID);
    assert_int_equal(rkvc_decoder_close(decoder), RKVC_OK);
}

static rkvc_stream *make_fake_stream(rkvc_stream_dir direction)
{
    rkvc_stream *stream = rkvc_calloc(1, sizeof(*stream));
    assert_non_null(stream);

    stream->config = rkvc_stream_config_defaults();
    stream->config.direction = direction;
    pthread_mutex_init(&stream->buf_lock, NULL);
    pthread_cond_init(&stream->buf_not_full, NULL);
    pthread_cond_init(&stream->buf_not_empty, NULL);

    return stream;
}

static void test_stream_fake_stats_and_paths(void **state)
{
    (void)state;

    rkvc_packet pkt = {0};
    rkvc_frame *frame = NULL;
    rkvc_stream_stats stats;
    rkvc_stream *stream = make_fake_stream(RKVC_STREAM_ENCODE);

    stream->stats.frames_out = 3;
    stream->first_out_time = av_gettime_relative() - 1000000;
    assert_int_equal(rkvc_stream_get_stats(stream, &stats), RKVC_OK);
    assert_true(stats.avg_fps > 0.0);

    assert_int_equal(rkvc_stream_push(stream, &pkt), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_stream_pull(stream, &pkt, 0), RKVC_ERR_INVALID);
    stream->finished = 1;
    assert_int_equal(rkvc_stream_push(stream, &pkt), RKVC_ERR_EOF);
    assert_int_equal(rkvc_stream_finish(stream), RKVC_OK);
    assert_int_equal(rkvc_stream_close(stream), RKVC_OK);

    stream = make_fake_stream(RKVC_STREAM_DECODE);
    assert_int_equal(rkvc_stream_push(stream, &pkt), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_stream_pull(stream, &frame, 0), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_stream_finish(stream), RKVC_OK);
    assert_int_equal(rkvc_stream_close(stream), RKVC_OK);

    stream = make_fake_stream(RKVC_STREAM_ENCODE);
    stream->enc = make_fake_encoder(RKVC_PIX_FMT_NV12);
    assert_int_equal(rkvc_stream_finish(stream), RKVC_OK);
    assert_int_equal(rkvc_stream_close(stream), RKVC_OK);

    stream = make_fake_stream(RKVC_STREAM_DECODE);
    stream->dec = make_fake_decoder();
    assert_int_equal(rkvc_stream_finish(stream), RKVC_OK);
    assert_int_equal(rkvc_stream_close(stream), RKVC_OK);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_averror_mapping),
        cmocka_unit_test(test_enum_validators),
        cmocka_unit_test(test_pixel_format_round_trip),
        cmocka_unit_test(test_frame_wrap_contracts),
        cmocka_unit_test(test_encoder_fake_getters_and_flushed_paths),
#ifdef RKVC_ENABLE_FAULT_INJECTION
        cmocka_unit_test(test_encoder_pure_helper_paths),
#endif
        cmocka_unit_test(test_encoder_send_buffer_layouts),
        cmocka_unit_test(test_encoder_send_buffer_rejects_bad_linesize),
        cmocka_unit_test(test_encoder_fake_active_error_paths),
        cmocka_unit_test(test_decoder_fake_getters_and_flushed_paths),
        cmocka_unit_test(test_stream_fake_stats_and_paths),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
