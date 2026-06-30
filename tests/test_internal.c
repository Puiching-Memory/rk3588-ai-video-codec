/**
 * @file test_internal.c
 * @brief v2 内部映射与 buffer 包装测试。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "internal.h"

static void test_averror_mapping(void **state)
{
    (void)state;
    assert_int_equal(rkvc_from_averror(AVERROR(ENOMEM)), RKVC_ERR_NOMEM);
    assert_int_equal(rkvc_from_averror(AVERROR(EAGAIN)), RKVC_ERR_AGAIN);
    assert_int_equal(rkvc_from_averror(AVERROR_EOF), RKVC_ERR_EOF);
}

static void test_pix_fmt_roundtrip(void **state)
{
    (void)state;
    assert_int_equal(rkvc_to_av_pix_fmt(RKVC_PIX_FMT_NV12), AV_PIX_FMT_NV12);
    assert_int_equal(rkvc_from_av_pix_fmt(AV_PIX_FMT_NV12), RKVC_PIX_FMT_NV12);
}

static void test_buffer_wrap_avframe(void **state)
{
    (void)state;
    AVFrame *av = av_frame_alloc();
    av->width = 64;
    av->height = 64;
    av->format = AV_PIX_FMT_NV12;
    assert_int_equal(rkvc_avframe_alloc_contiguous(av), RKVC_OK);

    rkvc_buffer *b = rkvc_buffer_wrap_avframe(av, 1);
    assert_non_null(b);
    rkvc_buffer_video_info info;
    assert_int_equal(rkvc_buffer_get_video_info(b, &info), RKVC_OK);
    assert_int_equal(info.width, 64u);
    rkvc_buffer_unref(b);
}

static void test_port_queue(void **state)
{
    (void)state;
    rkvc_port_queue *q = rkvc_port_queue_create(2);
    rkvc_buffer *b = NULL;
    rkvc_buffer_alloc_video_host(&b, 16, 16, RKVC_PIX_FMT_NV12);
    assert_int_equal(rkvc_port_queue_push(q, b), RKVC_OK);
    rkvc_buffer *out = NULL;
    assert_int_equal(rkvc_port_queue_pull(q, &out, 0), RKVC_OK);
    rkvc_buffer_unref(out);
    rkvc_buffer_unref(b);
    rkvc_port_queue_destroy(q);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_averror_mapping),
        cmocka_unit_test(test_pix_fmt_roundtrip),
        cmocka_unit_test(test_buffer_wrap_avframe),
        cmocka_unit_test(test_port_queue),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
