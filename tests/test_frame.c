/**
 * @file test_frame.c
 * @brief 帧生命周期和元数据单元测试 (CMocka)。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "rkvc/rkvc.h"

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

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_frame_alloc_nv12),
        cmocka_unit_test(test_frame_alloc_yuv420p),
        cmocka_unit_test(test_frame_get_data),
        cmocka_unit_test(test_frame_write_read_pixel),
        cmocka_unit_test(test_frame_set_pts),
        cmocka_unit_test(test_frame_ref_unref),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
