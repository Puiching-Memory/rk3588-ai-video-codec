/**
 * @file test_contracts.c
 * @brief 公共 API 契约和异常路径单元测试 (CMocka)。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
#include "rkvc/rkvc.h"

static void assert_encoder_invalid(rkvc_encoder_config cfg)
{
    rkvc_encoder *enc = (rkvc_encoder *)(uintptr_t)0x1;
    assert_int_equal(rkvc_encoder_open(&enc, &cfg), RKVC_ERR_INVALID);
    assert_null(enc);
}

static void assert_decoder_invalid(rkvc_decoder_config cfg)
{
    rkvc_decoder *dec = (rkvc_decoder *)(uintptr_t)0x1;
    assert_int_equal(rkvc_decoder_open(&dec, &cfg), RKVC_ERR_INVALID);
    assert_null(dec);
}

static void assert_stream_invalid(rkvc_stream_config cfg)
{
    rkvc_stream *stream = (rkvc_stream *)(uintptr_t)0x1;
    assert_int_equal(rkvc_stream_open(&stream, &cfg), RKVC_ERR_INVALID);
    assert_null(stream);
}

static void test_err_str_unknown_code(void **state)
{
    (void)state;
    assert_string_equal(rkvc_err_str((rkvc_err)12345), "unknown error");
}

static void test_query_caps_contracts(void **state)
{
    (void)state;

    rkvc_caps caps;
    assert_int_equal(rkvc_query_caps(NULL), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_query_caps(&caps), RKVC_OK);
    assert_true(caps.has_rkmpp_enc == 0 || caps.has_rkmpp_enc == 1);
    assert_true(caps.has_rkmpp_dec == 0 || caps.has_rkmpp_dec == 1);
    assert_true(caps.has_dma_heap == 0 || caps.has_dma_heap == 1);
    assert_true(caps.has_rga == 0 || caps.has_rga == 1);
    assert_true(caps.max_width >= 1920);
    assert_true(caps.max_height >= 1080);
}

static void test_encoder_open_file_rejects_null_or_empty_path(void **state)
{
    (void)state;

    rkvc_encoder *enc = NULL;
    rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
    assert_int_equal(rkvc_encoder_open_file(&enc, &cfg, NULL), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_encoder_open_file(&enc, &cfg, ""), RKVC_ERR_INVALID);
    assert_null(enc);
}

static void test_encoder_api_null_contracts(void **state)
{
    (void)state;

    rkvc_packet pkt;
    rkvc_encoder_config cfg;
    uint8_t byte = 0;
    int num = 0;
    int den = 0;

    assert_int_equal(rkvc_encoder_send_frame(NULL, NULL), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_encoder_send_buffer(NULL, &byte, 1, 0),
                     RKVC_ERR_INVALID);
    assert_int_equal(rkvc_encoder_receive_packet(NULL, &pkt),
                     RKVC_ERR_INVALID);
    assert_int_equal(rkvc_encoder_drain(NULL), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_encoder_timebase(NULL, &num, &den),
                     RKVC_ERR_INVALID);
    assert_int_equal(rkvc_encoder_get_config(NULL, &cfg), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_encoder_close(NULL), RKVC_OK);
}

static void test_encoder_open_rejects_invalid_configs(void **state)
{
    (void)state;

    rkvc_encoder_config cfg = rkvc_encoder_config_defaults();

    cfg.width = 0;
    assert_encoder_invalid(cfg);

    cfg = rkvc_encoder_config_defaults();
    cfg.height = 0;
    assert_encoder_invalid(cfg);

    cfg = rkvc_encoder_config_defaults();
    cfg.fps_num = 0;
    assert_encoder_invalid(cfg);

    cfg = rkvc_encoder_config_defaults();
    cfg.fps_den = 0;
    assert_encoder_invalid(cfg);

    cfg = rkvc_encoder_config_defaults();
    cfg.bitrate = 0;
    assert_encoder_invalid(cfg);

    cfg = rkvc_encoder_config_defaults();
    cfg.gop_size = 0;
    assert_encoder_invalid(cfg);

    cfg = rkvc_encoder_config_defaults();
    cfg.input_format = (rkvc_pix_fmt)999;
    assert_encoder_invalid(cfg);

    cfg = rkvc_encoder_config_defaults();
    cfg.preset = (rkvc_preset)999;
    assert_encoder_invalid(cfg);

    cfg = rkvc_encoder_config_defaults();
    cfg.rc_mode = (rkvc_rc_mode)999;
    assert_encoder_invalid(cfg);

    cfg = rkvc_encoder_config_defaults();
    cfg.qp = -1;
    assert_encoder_invalid(cfg);

    cfg = rkvc_encoder_config_defaults();
    cfg.num_b_frames = -1;
    assert_encoder_invalid(cfg);

    cfg = rkvc_encoder_config_defaults();
    cfg.threads = -1;
    assert_encoder_invalid(cfg);
}

static void test_decoder_open_file_rejects_null_or_empty_path(void **state)
{
    (void)state;

    rkvc_decoder *dec = NULL;
    rkvc_decoder_config cfg = rkvc_decoder_config_defaults();
    assert_int_equal(rkvc_decoder_open_file(&dec, &cfg, NULL), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_decoder_open_file(&dec, &cfg, ""), RKVC_ERR_INVALID);
    assert_null(dec);
}

static void test_decoder_open_file_rejects_missing_path(void **state)
{
    (void)state;

    rkvc_decoder *dec = (rkvc_decoder *)(uintptr_t)0x1;
    rkvc_decoder_config cfg = rkvc_decoder_config_defaults();
    assert_int_equal(rkvc_decoder_open_file(
                         &dec, &cfg, "/tmp/rkvc-definitely-missing-input.h265"),
                     RKVC_ERR_NOT_FOUND);
    assert_null(dec);
}

static void test_decoder_api_null_contracts(void **state)
{
    (void)state;

    rkvc_frame *frame = NULL;
    int width = 0;
    int height = 0;
    int fps_num = 0;
    int fps_den = 0;
    int64_t duration_us = 0;
    const uint8_t byte = 0;

    assert_int_equal(rkvc_decoder_send_packet(NULL, &byte, 1, 0, 0),
                     RKVC_ERR_INVALID);
    assert_int_equal(rkvc_decoder_read_packet(NULL), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_decoder_receive_frame(NULL, &frame),
                     RKVC_ERR_INVALID);
    assert_int_equal(rkvc_decoder_drain(NULL), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_decoder_get_video_info(NULL, &width, &height,
                                                 &fps_num, &fps_den),
                     RKVC_ERR_INVALID);
    assert_int_equal(rkvc_decoder_get_duration(NULL, &duration_us),
                     RKVC_ERR_INVALID);
    assert_int_equal(rkvc_decoder_close(NULL), RKVC_OK);
}

static void test_decoder_open_rejects_invalid_configs(void **state)
{
    (void)state;

    rkvc_decoder_config cfg = rkvc_decoder_config_defaults();

    cfg.output_format = (rkvc_pix_fmt)999;
    assert_decoder_invalid(cfg);

    cfg = rkvc_decoder_config_defaults();
    cfg.threads = -1;
    assert_decoder_invalid(cfg);

    cfg = rkvc_decoder_config_defaults();
    cfg.low_delay = 2;
    assert_decoder_invalid(cfg);
}

static void test_stream_open_rejects_invalid_configs(void **state)
{
    (void)state;

    rkvc_stream_config cfg = rkvc_stream_config_defaults();

    cfg.direction = (rkvc_stream_dir)99;
    assert_stream_invalid(cfg);

    cfg = rkvc_stream_config_defaults();
    cfg.buffer_size = 0;
    assert_stream_invalid(cfg);

    cfg = rkvc_stream_config_defaults();
    cfg.drop_frames = 2;
    assert_stream_invalid(cfg);

    cfg = rkvc_stream_config_defaults();
    cfg.width = 0;
    assert_stream_invalid(cfg);

    cfg = rkvc_stream_config_defaults();
    cfg.height = 0;
    assert_stream_invalid(cfg);

    cfg = rkvc_stream_config_defaults();
    cfg.fps_num = 0;
    assert_stream_invalid(cfg);

    cfg = rkvc_stream_config_defaults();
    cfg.fps_den = 0;
    assert_stream_invalid(cfg);

    cfg = rkvc_stream_config_defaults();
    cfg.bitrate = 0;
    assert_stream_invalid(cfg);

    cfg = rkvc_stream_config_defaults();
    cfg.input_format = (rkvc_pix_fmt)999;
    assert_stream_invalid(cfg);

    cfg = rkvc_stream_config_defaults();
    cfg.preset = (rkvc_preset)999;
    assert_stream_invalid(cfg);

    cfg = rkvc_stream_config_defaults();
    cfg.direction = RKVC_STREAM_DECODE;
    cfg.output_format = (rkvc_pix_fmt)999;
    assert_stream_invalid(cfg);
}

static void test_stream_api_null_contracts(void **state)
{
    (void)state;

    rkvc_stream_stats stats;
    rkvc_packet pkt;
    rkvc_frame *frame = NULL;

    assert_int_equal(rkvc_stream_open(NULL, NULL), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_stream_push(NULL, &pkt), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_stream_pull(NULL, &pkt, 0), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_stream_pull((rkvc_stream *)(uintptr_t)0x1, NULL, 0), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_stream_finish(NULL), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_stream_get_stats(NULL, &stats), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_stream_get_stats((rkvc_stream *)(uintptr_t)0x1, NULL), RKVC_ERR_INVALID);

    rkvc_frame_unref(frame);
    assert_int_equal(rkvc_stream_close(NULL), RKVC_OK);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_err_str_unknown_code),
        cmocka_unit_test(test_query_caps_contracts),
        cmocka_unit_test(test_encoder_open_file_rejects_null_or_empty_path),
        cmocka_unit_test(test_encoder_api_null_contracts),
        cmocka_unit_test(test_encoder_open_rejects_invalid_configs),
        cmocka_unit_test(test_decoder_open_file_rejects_null_or_empty_path),
        cmocka_unit_test(test_decoder_open_file_rejects_missing_path),
        cmocka_unit_test(test_decoder_api_null_contracts),
        cmocka_unit_test(test_decoder_open_rejects_invalid_configs),
        cmocka_unit_test(test_stream_open_rejects_invalid_configs),
        cmocka_unit_test(test_stream_api_null_contracts),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
