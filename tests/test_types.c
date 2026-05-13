/**
 * @file test_types.c
 * @brief 类型系统和公共 API 单元测试 (CMocka)。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "rkvc/rkvc.h"

static void test_version_string_not_null(void **state) {
    (void)state;
    assert_non_null(rkvc_version());
}

static void test_version_number_nonzero(void **state) {
    (void)state;
    assert_true(rkvc_version_number() > 0);
}

static void test_err_str_ok(void **state) {
    (void)state;
    const char *s = rkvc_err_str(RKVC_OK);
    assert_non_null(s);
    assert_string_not_equal(s, "");
}

static void test_err_str_all_codes(void **state) {
    (void)state;
    rkvc_err codes[] = {
        RKVC_OK, RKVC_ERR_NOMEM, RKVC_ERR_INVALID,
        RKVC_ERR_NOT_FOUND, RKVC_ERR_IO, RKVC_ERR_HW,
        RKVC_ERR_EOF, RKVC_ERR_AGAIN, RKVC_ERR_MUX,
        RKVC_ERR_INTERNAL,
    };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
        assert_non_null(rkvc_err_str(codes[i]));
}

static void test_encoder_config_defaults(void **state) {
    (void)state;
    rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
    assert_int_equal(cfg.width, 1920);
    assert_int_equal(cfg.height, 1080);
    assert_int_equal(cfg.fps_num, 30);
    assert_true(cfg.bitrate > 0);
    assert_int_equal(cfg.input_format, RKVC_PIX_FMT_NV12);
}

static void test_decoder_config_defaults(void **state) {
    (void)state;
    rkvc_decoder_config cfg = rkvc_decoder_config_defaults();
    assert_int_equal(cfg.output_format, RKVC_PIX_FMT_NV12);
}

static void test_stream_config_defaults(void **state) {
    (void)state;
    rkvc_stream_config cfg = rkvc_stream_config_defaults();
    assert_int_equal(cfg.direction, RKVC_STREAM_ENCODE);
    assert_int_equal(cfg.width, 1920);
}

static void test_encoder_open_null(void **state) {
    (void)state;
    assert_int_equal(rkvc_encoder_open(NULL, NULL), RKVC_ERR_INVALID);
}

static void test_decoder_open_null(void **state) {
    (void)state;
    assert_int_equal(rkvc_decoder_open(NULL, NULL), RKVC_ERR_INVALID);
}

static void test_frame_alloc_zero(void **state) {
    (void)state;
    rkvc_frame *f = NULL;
    assert_int_equal(rkvc_frame_alloc(&f, 0, 0, RKVC_PIX_FMT_NV12), RKVC_ERR_INVALID);
    assert_null(f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_version_string_not_null),
        cmocka_unit_test(test_version_number_nonzero),
        cmocka_unit_test(test_err_str_ok),
        cmocka_unit_test(test_err_str_all_codes),
        cmocka_unit_test(test_encoder_config_defaults),
        cmocka_unit_test(test_decoder_config_defaults),
        cmocka_unit_test(test_stream_config_defaults),
        cmocka_unit_test(test_encoder_open_null),
        cmocka_unit_test(test_decoder_open_null),
        cmocka_unit_test(test_frame_alloc_zero),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
