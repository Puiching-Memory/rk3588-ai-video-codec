/**
 * @file test_frame.c
 * @brief 帧生命周期和元数据单元测试 (CMocka)。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
#include "rkvc/rkvc.h"

typedef struct {
    rkvc_pix_fmt format;
    int min_expected_planes;
} frame_format_case;

static const frame_format_case k_frame_formats[] = {
    {RKVC_PIX_FMT_NV12, 2},
    {RKVC_PIX_FMT_YUV420P, 3},
    {RKVC_PIX_FMT_NV16, 2},
    {RKVC_PIX_FMT_P010, 2},
};

static void assert_plane_layout(const rkvc_frame *f, rkvc_pix_fmt format,
                                int expected_min_planes)
{
    uint8_t *planes[4] = {0};
    int strides[4] = {0};

    assert_int_equal(rkvc_frame_get_data(f, planes, strides), RKVC_OK);
    for (int i = 0; i < expected_min_planes; i++) {
        assert_non_null(planes[i]);
        assert_true(strides[i] > 0);
    }

    if (format == RKVC_PIX_FMT_YUV420P)
        assert_non_null(planes[2]);
}

static void test_frame_alloc_nv12(void **state) {
    (void)state;
    rkvc_frame *f = NULL;
    assert_int_equal(rkvc_frame_alloc(&f, 640, 480, RKVC_PIX_FMT_NV12), RKVC_OK);
    assert_non_null(f);

    rkvc_frame_info info;
    assert_int_equal(rkvc_frame_get_info(f, &info), RKVC_OK);
    assert_int_equal(info.width, 640);
    assert_int_equal(info.height, 480);
    assert_int_equal(info.format, RKVC_PIX_FMT_NV12);
    rkvc_frame_unref(f);
}

static void test_frame_alloc_yuv420p(void **state) {
    (void)state;
    rkvc_frame *f = NULL;
    assert_int_equal(rkvc_frame_alloc(&f, 320, 240, RKVC_PIX_FMT_YUV420P), RKVC_OK);

    rkvc_frame_info info;
    rkvc_frame_get_info(f, &info);
    assert_int_equal(info.format, RKVC_PIX_FMT_YUV420P);
    rkvc_frame_unref(f);
}

static void test_frame_get_data(void **state) {
    (void)state;
    rkvc_frame *f = NULL;
    rkvc_frame_alloc(&f, 640, 480, RKVC_PIX_FMT_NV12);

    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    assert_int_equal(rkvc_frame_get_data(f, planes, strides), RKVC_OK);
    assert_non_null(planes[0]);
    assert_true(strides[0] > 0);
    rkvc_frame_unref(f);
}

static void test_frame_write_read_pixel(void **state) {
    (void)state;
    rkvc_frame *f = NULL;
    rkvc_frame_alloc(&f, 640, 480, RKVC_PIX_FMT_NV12);

    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_get_data(f, planes, strides);

    planes[0][0] = 42;
    planes[0][100] = 0xAB;
    assert_int_equal(planes[0][0], 42);
    assert_int_equal(planes[0][100], 0xAB);
    rkvc_frame_unref(f);
}

static void test_frame_set_pts(void **state) {
    (void)state;
    rkvc_frame *f = NULL;
    rkvc_frame_alloc(&f, 640, 480, RKVC_PIX_FMT_NV12);

    rkvc_frame_set_pts(f, 12345);
    rkvc_frame_info info;
    rkvc_frame_get_info(f, &info);
    assert_int_equal(info.pts, 12345);
    rkvc_frame_unref(f);
}

static void test_frame_ref_unref(void **state) {
    (void)state;
    rkvc_frame *f = NULL;
    rkvc_frame_alloc(&f, 640, 480, RKVC_PIX_FMT_NV12);

    rkvc_frame *f2 = rkvc_frame_ref(f);
    assert_ptr_equal(f2, f);

    rkvc_frame_unref(f2);

    rkvc_frame_info info;
    assert_int_equal(rkvc_frame_get_info(f, &info), RKVC_OK);

    rkvc_frame_unref(f);
}

static void test_frame_all_supported_formats(void **state) {
    (void)state;

    for (size_t i = 0; i < sizeof(k_frame_formats) / sizeof(k_frame_formats[0]); i++) {
        rkvc_frame *f = NULL;
        assert_int_equal(rkvc_frame_alloc(&f, 128, 72, k_frame_formats[i].format), RKVC_OK);
        assert_non_null(f);

        rkvc_frame_info info;
        assert_int_equal(rkvc_frame_get_info(f, &info), RKVC_OK);
        assert_int_equal(info.width, 128);
        assert_int_equal(info.height, 72);
        assert_int_equal(info.format, k_frame_formats[i].format);

        assert_plane_layout(f, k_frame_formats[i].format,
                            k_frame_formats[i].min_expected_planes);
        rkvc_frame_unref(f);
    }
}

static void test_frame_invalid_argument_contracts(void **state) {
    (void)state;

    rkvc_frame *f = (rkvc_frame *)(uintptr_t)0x1;
    assert_int_equal(rkvc_frame_alloc(NULL, 64, 64, RKVC_PIX_FMT_NV12), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_frame_alloc(&f, 0, 64, RKVC_PIX_FMT_NV12), RKVC_ERR_INVALID);
    assert_null(f);

    f = (rkvc_frame *)(uintptr_t)0x1;
    assert_int_equal(rkvc_frame_alloc(&f, 64, 0, RKVC_PIX_FMT_NV12), RKVC_ERR_INVALID);
    assert_null(f);

    f = (rkvc_frame *)(uintptr_t)0x1;
    assert_int_equal(rkvc_frame_alloc(&f, 64, 64, (rkvc_pix_fmt)999), RKVC_ERR_INVALID);
    assert_null(f);

    assert_int_equal(rkvc_frame_get_info(NULL, NULL), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_frame_get_data(NULL, NULL, NULL), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_frame_set_pts(NULL, 1), RKVC_ERR_INVALID);
    assert_null(rkvc_frame_ref(NULL));
    rkvc_frame_unref(NULL);

    assert_int_equal(rkvc_frame_alloc(&f, 64, 64, RKVC_PIX_FMT_NV12), RKVC_OK);
    assert_non_null(f);

    rkvc_frame_info info;
    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    assert_int_equal(rkvc_frame_get_info(NULL, &info), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_frame_get_info(f, NULL), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_frame_get_data(f, NULL, strides), RKVC_ERR_INVALID);
    assert_int_equal(rkvc_frame_get_data(f, planes, NULL), RKVC_ERR_INVALID);

    rkvc_frame_unref(f);
}

static void test_frame_pts_round_trip_multiple_values(void **state) {
    (void)state;

    const int64_t pts_values[] = {0, 1, -1, 1234567890123LL};
    rkvc_frame *f = NULL;
    assert_int_equal(rkvc_frame_alloc(&f, 64, 64, RKVC_PIX_FMT_NV12), RKVC_OK);

    for (size_t i = 0; i < sizeof(pts_values) / sizeof(pts_values[0]); i++) {
        rkvc_frame_info info;
        assert_int_equal(rkvc_frame_set_pts(f, pts_values[i]), RKVC_OK);
        assert_int_equal(rkvc_frame_get_info(f, &info), RKVC_OK);
        assert_int_equal(info.pts, pts_values[i]);
    }

    rkvc_frame_unref(f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_frame_alloc_nv12),
        cmocka_unit_test(test_frame_alloc_yuv420p),
        cmocka_unit_test(test_frame_get_data),
        cmocka_unit_test(test_frame_write_read_pixel),
        cmocka_unit_test(test_frame_set_pts),
        cmocka_unit_test(test_frame_ref_unref),
        cmocka_unit_test(test_frame_all_supported_formats),
        cmocka_unit_test(test_frame_invalid_argument_contracts),
        cmocka_unit_test(test_frame_pts_round_trip_multiple_values),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
