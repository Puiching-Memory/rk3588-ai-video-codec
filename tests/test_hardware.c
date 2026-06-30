/**
 * @file test_hardware.c
 * @brief Session 级硬件集成测试（需 RKVC_RUN_HARDWARE_TESTS=1）。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <cmocka.h>
#include "rkvc/rkvc.h"
#include "test_support.h"

/** mkstemp 要求 XXXXXX 在路径末尾；生成带后缀的空临时文件供 session 写入。 */
static int test_mktemp_file(char *out_path, size_t out_sz,
                            const char *prefix, const char *suffix)
{
    char tmpl[PATH_MAX];
    snprintf(tmpl, sizeof(tmpl), "%sXXXXXX", prefix);
    int fd = mkstemp(tmpl);
    if (fd < 0)
        return -1;
    close(fd);

    if (suffix && suffix[0]) {
        snprintf(out_path, out_sz, "%s%s", tmpl, suffix);
        if (rename(tmpl, out_path) != 0) {
            unlink(tmpl);
            return -1;
        }
    } else {
        snprintf(out_path, out_sz, "%s", tmpl);
    }
    return 0;
}

static const char *fixture_h264(void)
{
    const char *p = getenv("RKVC_TEST_INPUT_H264");
    return p && p[0] ? p : "tests/fixtures/sample.h264.mp4";
}

static void skip_unless_hw(void)
{
    if (!rkvc_test_hardware_opted_in())
        skip();
    rkvc_init();
    rkvc_caps caps;
    if (rkvc_query_caps(&caps) != RKVC_OK)
        skip();
    if (!caps.has_h264_dec || !caps.has_h264_enc)
        skip();
}

static void test_session_transcode_h264(void **state)
{
    (void)state;
    skip_unless_hw();

    char out[PATH_MAX];
    if (test_mktemp_file(out, sizeof(out), "/tmp/rkvc_v2_transcode_", ".mp4") != 0)
        skip();

    rkvc_pipeline_desc d;
    rkvc_pipeline_from_template(RKVC_TEMPLATE_FILE_TRANSCODE, &d);
    d.policy      = RKVC_POLICY_REALTIME;
    d.input_path  = fixture_h264();
    d.output_path = out;
    d.width       = 1920;
    d.height      = 1080;

    rkvc_session *s = NULL;
    assert_int_equal(rkvc_session_create(&d, &s), RKVC_OK);
    rkvc_err err = rkvc_session_run_file(s);
    rkvc_session_destroy(s);
    remove(out);

    if (err == RKVC_ERR_IO || err == RKVC_ERR_NOT_FOUND)
        skip();
    assert_int_equal(err, RKVC_OK);
}

static void test_session_decode_nv12(void **state)
{
    (void)state;
    skip_unless_hw();

    char out[PATH_MAX];
    if (test_mktemp_file(out, sizeof(out), "/tmp/rkvc_v2_decode_", ".nv12") != 0)
        skip();

    rkvc_pipeline_desc d;
    rkvc_pipeline_from_template(RKVC_TEMPLATE_FILE_DECODE, &d);
    d.input_path  = fixture_h264();
    d.output_path = out;

    rkvc_session *s = NULL;
    rkvc_session_create(&d, &s);
    rkvc_err err = rkvc_session_run_file(s);
    rkvc_session_destroy(s);
    remove(out);
    if (err == RKVC_ERR_IO || err == RKVC_ERR_NOT_FOUND)
        skip();
    assert_int_equal(err, RKVC_OK);
}

static void test_session_encode_h264(void **state)
{
    (void)state;
    skip_unless_hw();

    const char *raw = getenv("RKVC_TEST_RAW_NV12");
    if (!raw || !raw[0])
        skip();

    char out[PATH_MAX];
    if (test_mktemp_file(out, sizeof(out), "/tmp/rkvc_v2_encode_", ".mp4") != 0)
        skip();

    rkvc_pipeline_desc d;
    rkvc_pipeline_from_template(RKVC_TEMPLATE_FILE_ENCODE, &d);
    d.input_path  = raw;
    d.output_path = out;
    d.policy      = RKVC_POLICY_REALTIME;

    rkvc_session *s = NULL;
    rkvc_session_create(&d, &s);
    rkvc_err err = rkvc_session_run_file(s);
    rkvc_session_destroy(s);
    remove(out);
    assert_int_equal(err, RKVC_OK);
}

static void test_session_av1_storage(void **state)
{
    (void)state;
    if (!rkvc_test_hardware_opted_in())
        skip();
    rkvc_init();

    rkvc_caps caps;
    rkvc_query_caps(&caps);
    if (!caps.has_av1_dec)
        skip();

    char out[PATH_MAX];
    if (test_mktemp_file(out, sizeof(out), "/tmp/rkvc_v2_av1_", ".mp4") != 0)
        skip();

    rkvc_pipeline_desc d;
    rkvc_pipeline_from_template(RKVC_TEMPLATE_AV1_STORAGE, &d);
    d.input_path  = fixture_h264();
    d.output_path = out;

    rkvc_session *s = NULL;
    rkvc_session_create(&d, &s);
    rkvc_err err = rkvc_session_run_file(s);
    rkvc_session_destroy(s);
    remove(out);
    if (err == RKVC_ERR_IO || err == RKVC_ERR_NOT_FOUND)
        skip();
    assert_true(err == RKVC_OK || err == RKVC_ERR_NOT_FOUND);
}

static void test_session_encode_decode_upscale_3x(void **state)
{
    (void)state;
    skip_unless_hw();

    rkvc_caps caps;
    rkvc_query_caps(&caps);
    if (!caps.has_rga)
        skip();

    const char *raw = getenv("RKVC_TEST_RAW_NV12");
    if (!raw || !raw[0])
        skip();

    char enc_path[PATH_MAX];
    char dec_path[PATH_MAX];
    if (test_mktemp_file(enc_path, sizeof(enc_path), "/tmp/rkvc_v2_enc3x_", ".mp4") != 0)
        skip();
    if (test_mktemp_file(dec_path, sizeof(dec_path), "/tmp/rkvc_v2_dec3x_", ".nv12") != 0) {
        remove(enc_path);
        skip();
    }

    rkvc_pipeline_desc enc = {0};
    rkvc_pipeline_from_template(RKVC_TEMPLATE_FILE_ENCODE, &enc);
    enc.input_path       = raw;
    enc.output_path      = enc_path;
    enc.policy           = RKVC_POLICY_REALTIME;
    enc.width            = 1920;
    enc.height           = 1080;
    enc.enc_scale_denom  = 3;

    rkvc_session *s = NULL;
    assert_int_equal(rkvc_session_create(&enc, &s), RKVC_OK);
    rkvc_err err = rkvc_session_run_file(s);
    rkvc_session_destroy(s);
    assert_int_equal(err, RKVC_OK);

    rkvc_pipeline_desc dec = {0};
    rkvc_pipeline_from_template(RKVC_TEMPLATE_FILE_DECODE, &dec);
    dec.input_path        = enc_path;
    dec.output_path       = dec_path;
    dec.width             = 1920;
    dec.height            = 1080;
    dec.enc_scale_denom   = 3;
    dec.post_upscale_algo = RKVC_UPSCALE_BILINEAR;

    assert_int_equal(rkvc_session_create(&dec, &s), RKVC_OK);
    err = rkvc_session_run_file(s);
    rkvc_session_destroy(s);
    assert_int_equal(err, RKVC_OK);

    struct stat st;
    assert_int_equal(stat(dec_path, &st), 0);
    assert_true((size_t)st.st_size >= (size_t)1920 * 1080 * 3 / 2);

    remove(enc_path);
    remove(dec_path);
}

int main(int argc, char **argv)
{
    static const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_session_transcode_h264),
        cmocka_unit_test(test_session_decode_nv12),
        cmocka_unit_test(test_session_encode_h264),
        cmocka_unit_test(test_session_av1_storage),
        cmocka_unit_test(test_session_encode_decode_upscale_3x),
    };

    if (argc > 1) {
        for (size_t i = 0; tests[i].test_func != NULL; i++) {
            if (strcmp(argv[1], tests[i].name) == 0) {
                const struct CMUnitTest one[] = {
                    tests[i],
                    {0},
                };
                return cmocka_run_group_tests(one, NULL, NULL);
            }
        }
        fprintf(stderr, "unknown case: %s\n", argv[1]);
        return 1;
    }

    return cmocka_run_group_tests(tests, NULL, NULL);
}
