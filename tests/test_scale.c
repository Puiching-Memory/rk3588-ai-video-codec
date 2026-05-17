/**
 * @file test_scale.c
 * @brief RGA 硬件缩放单元测试 (CMocka)。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <time.h>
#include <cmocka.h>
#include "rkvc/rkvc.h"

#define RKVC_CTEST_SKIP 77

static void fill_nv12_test_pattern(rkvc_frame *frame, int seed)
{
    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_info info;

    rkvc_frame_get_info(frame, &info);
    rkvc_frame_get_data(frame, planes, strides);

    /* Y plane: gradient pattern */
    for (int y = 0; y < info.height; y++)
        for (int x = 0; x < info.width; x++)
            planes[0][y * strides[0] + x] =
                (uint8_t)((x + y + seed) & 0xff);

    /* UV plane: alternating chroma */
    for (int y = 0; y < info.height / 2; y++)
        for (int x = 0; x < info.width; x += 2) {
            planes[1][y * strides[1] + x]     = 128;
            planes[1][y * strides[1] + x + 1] = (uint8_t)(96 + seed);
        }
}

/* ── 可用性检测 ────────────────────────────────────────────────────── */

static void test_scale_available(void **state)
{
    (void)state;
    /* 只检查 API 可调用，不强制要求硬件存在 */
    int avail = rkvc_scale_available();
    assert_true(avail == 0 || avail == 1);
}

/* ── 参数校验 ──────────────────────────────────────────────────────── */

static void test_scale_null_src(void **state)
{
    (void)state;
    rkvc_frame *out = NULL;
    rkvc_scale_config cfg = { .dst_width = 320, .dst_height = 240 };
    assert_int_equal(rkvc_frame_scale(NULL, &out, &cfg), RKVC_ERR_INVALID);
}

static void test_scale_null_out(void **state)
{
    (void)state;
    rkvc_frame *src = NULL;
    rkvc_frame_alloc(&src, 640, 480, RKVC_PIX_FMT_NV12);
    rkvc_scale_config cfg = { .dst_width = 320, .dst_height = 240 };
    assert_int_equal(rkvc_frame_scale(src, NULL, &cfg), RKVC_ERR_INVALID);
    rkvc_frame_unref(src);
}

static void test_scale_null_cfg(void **state)
{
    (void)state;
    rkvc_frame *src = NULL;
    rkvc_frame *out = NULL;
    rkvc_frame_alloc(&src, 640, 480, RKVC_PIX_FMT_NV12);
    assert_int_equal(rkvc_frame_scale(src, &out, NULL), RKVC_ERR_INVALID);
    rkvc_frame_unref(src);
}

static void test_scale_zero_dimensions(void **state)
{
    (void)state;
    rkvc_frame *src = NULL;
    rkvc_frame *out = NULL;
    rkvc_frame_alloc(&src, 640, 480, RKVC_PIX_FMT_NV12);

    rkvc_scale_config cfg = { .dst_width = 0, .dst_height = 240 };
    assert_int_equal(rkvc_frame_scale(src, &out, &cfg), RKVC_ERR_INVALID);

    cfg.dst_width = 320;
    cfg.dst_height = 0;
    assert_int_equal(rkvc_frame_scale(src, &out, &cfg), RKVC_ERR_INVALID);

    rkvc_frame_unref(src);
}

/* ── 硬件缩放测试 (需要 RGA) ──────────────────────────────────────── */

static void skip_if_no_rga(void)
{
    if (!rkvc_scale_available()) {
        skip();
    }
}

static void test_scale_downscale_nv12(void **state)
{
    (void)state;
    skip_if_no_rga();

    rkvc_frame *src = NULL;
    rkvc_frame *dst = NULL;
    rkvc_frame_alloc(&src, 640, 480, RKVC_PIX_FMT_NV12);
    fill_nv12_test_pattern(src, 42);

    rkvc_scale_config cfg = {
        .dst_width  = 320,
        .dst_height = 240,
    };

    assert_int_equal(rkvc_frame_scale(src, &dst, &cfg), RKVC_OK);
    assert_non_null(dst);

    rkvc_frame_info info;
    rkvc_frame_get_info(dst, &info);
    assert_int_equal(info.width, 320);
    assert_int_equal(info.height, 240);
    assert_int_equal(info.format, RKVC_PIX_FMT_NV12);

    /* 验证输出帧有有效像素数据 */
    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_get_data(dst, planes, strides);
    assert_non_null(planes[0]);
    assert_non_null(planes[1]);
    assert_true(strides[0] > 0);

    rkvc_frame_unref(dst);
    rkvc_frame_unref(src);
}

static void test_scale_upscale_nv12(void **state)
{
    (void)state;
    skip_if_no_rga();

    rkvc_frame *src = NULL;
    rkvc_frame *dst = NULL;
    rkvc_frame_alloc(&src, 320, 240, RKVC_PIX_FMT_NV12);
    fill_nv12_test_pattern(src, 7);

    rkvc_scale_config cfg = {
        .dst_width  = 640,
        .dst_height = 480,
    };

    assert_int_equal(rkvc_frame_scale(src, &dst, &cfg), RKVC_OK);
    assert_non_null(dst);

    rkvc_frame_info info;
    rkvc_frame_get_info(dst, &info);
    assert_int_equal(info.width, 640);
    assert_int_equal(info.height, 480);

    rkvc_frame_unref(dst);
    rkvc_frame_unref(src);
}

static void test_scale_same_resolution(void **state)
{
    (void)state;
    skip_if_no_rga();

    rkvc_frame *src = NULL;
    rkvc_frame *dst = NULL;
    rkvc_frame_alloc(&src, 640, 480, RKVC_PIX_FMT_NV12);
    fill_nv12_test_pattern(src, 0);

    rkvc_scale_config cfg = {
        .dst_width  = 640,
        .dst_height = 480,
    };

    assert_int_equal(rkvc_frame_scale(src, &dst, &cfg), RKVC_OK);
    assert_non_null(dst);

    rkvc_frame_info info;
    rkvc_frame_get_info(dst, &info);
    assert_int_equal(info.width, 640);
    assert_int_equal(info.height, 480);

    rkvc_frame_unref(dst);
    rkvc_frame_unref(src);
}

static void test_scale_preserves_pts(void **state)
{
    (void)state;
    skip_if_no_rga();

    rkvc_frame *src = NULL;
    rkvc_frame *dst = NULL;
    rkvc_frame_alloc(&src, 640, 480, RKVC_PIX_FMT_NV12);
    rkvc_frame_set_pts(src, 12345);

    rkvc_scale_config cfg = {
        .dst_width  = 320,
        .dst_height = 240,
    };

    assert_int_equal(rkvc_frame_scale(src, &dst, &cfg), RKVC_OK);

    rkvc_frame_info info;
    rkvc_frame_get_info(dst, &info);
    assert_int_equal(info.pts, 12345);

    rkvc_frame_unref(dst);
    rkvc_frame_unref(src);
}

static void test_scale_to_h264_resolution(void **state)
{
    (void)state;
    skip_if_no_rga();

    /* 模拟真实场景: 2560x1440 → 1280x720 */
    rkvc_frame *src = NULL;
    rkvc_frame *dst = NULL;
    rkvc_frame_alloc(&src, 2560, 1440, RKVC_PIX_FMT_NV12);
    fill_nv12_test_pattern(src, 1);

    rkvc_scale_config cfg = {
        .dst_width  = 1280,
        .dst_height = 720,
    };

    assert_int_equal(rkvc_frame_scale(src, &dst, &cfg), RKVC_OK);
    assert_non_null(dst);

    rkvc_frame_info info;
    rkvc_frame_get_info(dst, &info);
    assert_int_equal(info.width, 1280);
    assert_int_equal(info.height, 720);

    rkvc_frame_unref(dst);
    rkvc_frame_unref(src);
}

/* ── stream 自动缩放测试 ──────────────────────────────────────────── */

static void test_stream_auto_scale(void **state)
{
    (void)state;
    skip_if_no_rga();

    /* 创建编码流: 目标 640x480 */
    rkvc_stream_config scfg = rkvc_stream_config_defaults();
    scfg.direction = RKVC_STREAM_ENCODE;
    scfg.width     = 640;
    scfg.height    = 480;
    scfg.fps_num   = 30;
    scfg.bitrate   = 1000000;

    rkvc_stream *stream = NULL;
    rkvc_err err = rkvc_stream_open(&stream, &scfg);
    if (err != RKVC_OK) {
        /* 编码器不可用时跳过 */
        skip();
    }

    /* 送入不同尺寸的帧 (320x240)，stream 应自动缩放 */
    rkvc_frame *f = NULL;
    rkvc_frame_alloc(&f, 320, 240, RKVC_PIX_FMT_NV12);
    fill_nv12_test_pattern(f, 0);
    rkvc_frame_set_pts(f, 0);

    err = rkvc_stream_push(stream, f);
    rkvc_frame_unref(f);

    /* push 应成功（自动缩放后编码） */
    assert_true(err == RKVC_OK || err == RKVC_ERR_AGAIN);

    /* flush 编码器，强制产出所有缓冲包 */
    rkvc_stream_finish(stream);

    /* pull 编码包 */
    rkvc_packet pkt;
    int got_packet = 0;
    while (rkvc_stream_pull(stream, &pkt, -1) == RKVC_OK) {
        got_packet = 1;
        assert_true(pkt.size > 0);
        break;
    }

    rkvc_stream_close(stream);

    /* 至少应产出一个编码包 */
    assert_int_equal(got_packet, 1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_scale_available),
        /* 参数校验 */
        cmocka_unit_test(test_scale_null_src),
        cmocka_unit_test(test_scale_null_out),
        cmocka_unit_test(test_scale_null_cfg),
        cmocka_unit_test(test_scale_zero_dimensions),
        /* 硬件缩放 */
        cmocka_unit_test(test_scale_downscale_nv12),
        cmocka_unit_test(test_scale_upscale_nv12),
        cmocka_unit_test(test_scale_same_resolution),
        cmocka_unit_test(test_scale_preserves_pts),
        cmocka_unit_test(test_scale_to_h264_resolution),
        /* stream 自动缩放 */
        cmocka_unit_test(test_stream_auto_scale),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
