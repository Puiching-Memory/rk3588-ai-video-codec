/**
 * @file test_fault_injection.c
 * @brief Deterministic OOM tests for project-owned allocations.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

#include "internal.h"

#ifndef RKVC_ENABLE_FAULT_INJECTION
#error "test_fault_injection.c requires RKVC_ENABLE_FAULT_INJECTION"
#endif

static int clear_faults(void **state)
{
    (void)state;
    rkvc_test_clear_faults();
    return 0;
}

static void test_frame_alloc_first_project_allocation_oom(void **state)
{
    (void)state;

    rkvc_frame *frame = (rkvc_frame *)(uintptr_t)0x1;
    rkvc_test_fail_alloc_after(0);

    assert_int_equal(rkvc_frame_alloc(&frame, 64, 64, RKVC_PIX_FMT_NV12),
                     RKVC_ERR_NOMEM);
    assert_null(frame);

    rkvc_test_clear_faults();
    assert_int_equal(rkvc_frame_alloc(&frame, 64, 64, RKVC_PIX_FMT_NV12),
                     RKVC_OK);
    rkvc_frame_unref(frame);
}

static void test_frame_wrap_first_project_allocation_oom(void **state)
{
    (void)state;

    AVFrame *av_frame = av_frame_alloc();
    assert_non_null(av_frame);
    av_frame->width = 16;
    av_frame->height = 16;
    av_frame->format = AV_PIX_FMT_NV12;

    rkvc_test_fail_alloc_after(0);
    assert_null(rkvc_frame_wrap_avframe(av_frame));

    rkvc_test_clear_faults();
    av_frame_free(&av_frame);
}

static void test_encoder_handle_allocation_oom(void **state)
{
    (void)state;

    rkvc_encoder *encoder = (rkvc_encoder *)(uintptr_t)0x1;
    rkvc_encoder_config cfg = rkvc_encoder_config_defaults();

    rkvc_test_fail_alloc_after(0);
    assert_int_equal(rkvc_encoder_open(&encoder, &cfg), RKVC_ERR_NOMEM);
    assert_null(encoder);
}

static void test_decoder_handle_allocation_oom(void **state)
{
    (void)state;

    rkvc_decoder *decoder = (rkvc_decoder *)(uintptr_t)0x1;
    rkvc_decoder_config cfg = rkvc_decoder_config_defaults();

    rkvc_test_fail_alloc_after(0);
    assert_int_equal(rkvc_decoder_open(&decoder, &cfg), RKVC_ERR_NOMEM);
    assert_null(decoder);
}

static void test_stream_handle_allocation_oom(void **state)
{
    (void)state;

    rkvc_stream *stream = (rkvc_stream *)(uintptr_t)0x1;
    rkvc_stream_config cfg = rkvc_stream_config_defaults();

    rkvc_test_fail_alloc_after(0);
    assert_int_equal(rkvc_stream_open(&stream, &cfg), RKVC_ERR_NOMEM);
    assert_null(stream);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_teardown(
            test_frame_alloc_first_project_allocation_oom, clear_faults),
        cmocka_unit_test_teardown(
            test_frame_wrap_first_project_allocation_oom, clear_faults),
        cmocka_unit_test_teardown(
            test_encoder_handle_allocation_oom, clear_faults),
        cmocka_unit_test_teardown(
            test_decoder_handle_allocation_oom, clear_faults),
        cmocka_unit_test_teardown(
            test_stream_handle_allocation_oom, clear_faults),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
