/**
 * @file test_scale.c
 * @brief RGA 硬件缩放单元测试 (CMocka)。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* ── 回归: 1080p NV12 UV 错位绿带 (issue: rkvc_frame_scale + RGA) ──────
 *
 * RGA 的 wrapbuffer_virtualaddr_t() 用单一基址 + wstride*hstride 推算
 * UV 平面位置；当帧的内存布局在 Y 与 UV 之间存在 padding (例如 ffmpeg
 * av_frame_get_buffer 对高度按 32 对齐 + 32 字节 plane padding)，RGA
 * 会把 UV 写到错误地址，造成帧底 16 行出现纯绿色带 (UV=(0,0))。
 *
 * 这组测试钉住两点：
 *   1. rkvc_frame_alloc 必须返回真正连续的平面布局；
 *   2. rkvc_frame_scale 同分辨率走 RGA 时输出必须与输入逐字节相同。
 * 选用 1080p 是因为高度 1080 不是 32 的倍数 (align→1088)，最容易触发。
 */

/* (1) 布局回归：纯 CPU 检查，不需要 RGA。 */
static void check_layout_contiguous_nv12(int w, int h)
{
    rkvc_frame *f = NULL;
    assert_int_equal(rkvc_frame_alloc(&f, w, h, RKVC_PIX_FMT_NV12), RKVC_OK);

    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_get_data(f, planes, strides);

    assert_non_null(planes[0]);
    assert_non_null(planes[1]);
    /* linesize 不应为 picture stride 引入额外行宽 padding。 */
    assert_int_equal(strides[0], w);
    assert_int_equal(strides[1], w);
    /* UV 必须紧贴 Y 之后；这里若失败说明 alloc 又退回到 padding 路径。 */
    assert_ptr_equal(planes[1], planes[0] + (ptrdiff_t)strides[0] * h);

    rkvc_frame_unref(f);
}

static void check_layout_contiguous_yuv420p(int w, int h)
{
    rkvc_frame *f = NULL;
    assert_int_equal(rkvc_frame_alloc(&f, w, h, RKVC_PIX_FMT_YUV420P),
                     RKVC_OK);

    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_get_data(f, planes, strides);

    assert_non_null(planes[0]);
    assert_non_null(planes[1]);
    assert_non_null(planes[2]);
    assert_int_equal(strides[0], w);
    assert_int_equal(strides[1], w / 2);
    assert_int_equal(strides[2], w / 2);
    assert_ptr_equal(planes[1], planes[0] + (ptrdiff_t)strides[0] * h);
    assert_ptr_equal(planes[2],
                     planes[1] + (ptrdiff_t)strides[1] * (h / 2));

    rkvc_frame_unref(f);
}

static void test_frame_alloc_contiguous_layout(void **state)
{
    (void)state;
    /* 1080p：高度 1080 不是 32 的倍数 → 历史上会引入 padding。 */
    check_layout_contiguous_nv12(1920, 1080);
    check_layout_contiguous_yuv420p(1920, 1080);
    /* 480p / 720p / 1440p 也覆盖一下，确认对齐余数为 0/16/0 时同样无 gap。 */
    check_layout_contiguous_nv12(640,  480);
    check_layout_contiguous_nv12(1280, 720);
    check_layout_contiguous_nv12(2560, 1440);
}

/* (2) 字节对等回归：RGA 同分辨率 imresize 必须等同于 imcopy。 */
static void fill_random_pattern(uint8_t *buf, size_t n, unsigned seed)
{
    /* 简单线性同余，避开极端均匀色块掩盖 UV 错位。 */
    uint32_t s = seed * 2654435761u + 1;
    for (size_t i = 0; i < n; i++) {
        s = s * 1103515245u + 12345u;
        buf[i] = (uint8_t)(s >> 16);
    }
}

static void copy_into_plane(uint8_t *dst, int dst_stride,
                            const uint8_t *src, int width, int height)
{
    for (int y = 0; y < height; y++)
        memcpy(dst + (size_t)y * dst_stride,
               src + (size_t)y * width, (size_t)width);
}

static void compare_plane_exact(const uint8_t *plane, int stride,
                                const uint8_t *ref, int width, int height,
                                const char *label)
{
    for (int y = 0; y < height; y++) {
        const uint8_t *row    = plane + (size_t)y * stride;
        const uint8_t *refrow = ref   + (size_t)y * width;
        if (memcmp(row, refrow, (size_t)width) != 0) {
            int first_x = 0;
            for (; first_x < width; first_x++)
                if (row[first_x] != refrow[first_x]) break;
            fprintf(stderr,
                    "[%s] mismatch row %d col %d: got=%u ref=%u\n",
                    label, y, first_x,
                    (unsigned)row[first_x], (unsigned)refrow[first_x]);
            fail_msg("%s plane mismatch at row %d col %d",
                     label, y, first_x);
        }
    }
}

/* 检查 NV12 帧底部不存在 UV=(0,0) 的绿带（修复前的特征伪影）。 */
static void assert_no_bottom_green_band_nv12(rkvc_frame *frame)
{
    rkvc_frame_info info;
    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_get_info(frame, &info);
    rkvc_frame_get_data(frame, planes, strides);

    /* 检查 UV 末尾 16 个 chroma 行（= 帧底 32 luma 行）是否全部 0。 */
    int chroma_rows = info.height / 2;
    int probe       = chroma_rows >= 16 ? 16 : chroma_rows;
    int all_zero_rows = 0;
    for (int y = chroma_rows - probe; y < chroma_rows; y++) {
        const uint8_t *row = planes[1] + (size_t)y * strides[1];
        int row_zero = 1;
        for (int x = 0; x < info.width; x++) {
            if (row[x] != 0) { row_zero = 0; break; }
        }
        if (row_zero) all_zero_rows++;
    }
    if (all_zero_rows >= 4) {
        fail_msg("UV plane has %d all-zero rows at bottom "
                 "(green-band regression)",
                 all_zero_rows);
    }
}

static void test_scale_identity_byte_exact_nv12_1080p(void **state)
{
    (void)state;
    skip_if_no_rga();

    const int W = 1920, H = 1080;

    /* 真值 = 连续 NV12 缓冲。 */
    size_t y_size  = (size_t)W * H;
    size_t uv_size = (size_t)W * (H / 2);
    uint8_t *ref_y  = malloc(y_size);
    uint8_t *ref_uv = malloc(uv_size);
    assert_non_null(ref_y);
    assert_non_null(ref_uv);
    fill_random_pattern(ref_y,  y_size,  0xC0DEC001u);
    fill_random_pattern(ref_uv, uv_size, 0xCAFEBABEu);

    rkvc_frame *src = NULL;
    assert_int_equal(rkvc_frame_alloc(&src, W, H, RKVC_PIX_FMT_NV12),
                     RKVC_OK);

    uint8_t *sp[4] = {0};
    int      ss[4] = {0};
    rkvc_frame_get_data(src, sp, ss);
    copy_into_plane(sp[0], ss[0], ref_y,  W, H);
    copy_into_plane(sp[1], ss[1], ref_uv, W, H / 2);

    rkvc_scale_config cfg = {
        .dst_width  = W,
        .dst_height = H,
        .dst_format = RKVC_PIX_FMT_NV12,
    };
    rkvc_frame *dst = NULL;
    assert_int_equal(rkvc_frame_scale(src, &dst, &cfg), RKVC_OK);
    assert_non_null(dst);

    uint8_t *dp[4] = {0};
    int      ds[4] = {0};
    rkvc_frame_get_data(dst, dp, ds);

    compare_plane_exact(dp[0], ds[0], ref_y,  W, H,     "Y");
    compare_plane_exact(dp[1], ds[1], ref_uv, W, H / 2, "UV");
    assert_no_bottom_green_band_nv12(dst);

    rkvc_frame_unref(dst);
    rkvc_frame_unref(src);
    free(ref_y);
    free(ref_uv);
}

static void test_scale_identity_byte_exact_yuv420p_1080p(void **state)
{
    (void)state;
    skip_if_no_rga();

    const int W = 1920, H = 1080;

    size_t y_size = (size_t)W * H;
    size_t c_size = (size_t)(W / 2) * (H / 2);
    uint8_t *ref_y = malloc(y_size);
    uint8_t *ref_u = malloc(c_size);
    uint8_t *ref_v = malloc(c_size);
    assert_non_null(ref_y);
    assert_non_null(ref_u);
    assert_non_null(ref_v);
    fill_random_pattern(ref_y, y_size, 0x11111111u);
    fill_random_pattern(ref_u, c_size, 0x22222222u);
    fill_random_pattern(ref_v, c_size, 0x33333333u);

    rkvc_frame *src = NULL;
    assert_int_equal(rkvc_frame_alloc(&src, W, H, RKVC_PIX_FMT_YUV420P),
                     RKVC_OK);

    uint8_t *sp[4] = {0};
    int      ss[4] = {0};
    rkvc_frame_get_data(src, sp, ss);
    copy_into_plane(sp[0], ss[0], ref_y, W,     H);
    copy_into_plane(sp[1], ss[1], ref_u, W / 2, H / 2);
    copy_into_plane(sp[2], ss[2], ref_v, W / 2, H / 2);

    rkvc_scale_config cfg = {
        .dst_width  = W,
        .dst_height = H,
        .dst_format = RKVC_PIX_FMT_YUV420P,
    };
    rkvc_frame *dst = NULL;
    assert_int_equal(rkvc_frame_scale(src, &dst, &cfg), RKVC_OK);
    assert_non_null(dst);

    uint8_t *dp[4] = {0};
    int      ds[4] = {0};
    rkvc_frame_get_data(dst, dp, ds);

    compare_plane_exact(dp[0], ds[0], ref_y, W,     H,     "Y");
    compare_plane_exact(dp[1], ds[1], ref_u, W / 2, H / 2, "U");
    compare_plane_exact(dp[2], ds[2], ref_v, W / 2, H / 2, "V");

    rkvc_frame_unref(dst);
    rkvc_frame_unref(src);
    free(ref_y);
    free(ref_u);
    free(ref_v);
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
        /* 帧布局回归 (与 RGA UV 错位绿带 bug 强相关) */
        cmocka_unit_test(test_frame_alloc_contiguous_layout),
        /* 硬件缩放 */
        cmocka_unit_test(test_scale_downscale_nv12),
        cmocka_unit_test(test_scale_upscale_nv12),
        cmocka_unit_test(test_scale_same_resolution),
        cmocka_unit_test(test_scale_preserves_pts),
        cmocka_unit_test(test_scale_to_h264_resolution),
        /* 1080p 字节对等回归: rkvc_frame_scale 不得在帧底产生绿带。 */
        cmocka_unit_test(test_scale_identity_byte_exact_nv12_1080p),
        cmocka_unit_test(test_scale_identity_byte_exact_yuv420p_1080p),
        /* stream 自动缩放 */
        cmocka_unit_test(test_stream_auto_scale),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
