/**
 * @file test_hardware.c
 * @brief RK3588 hardware encode/decode integration tests.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cmocka.h>

#include "rkvc/rkvc.h"

#define RKVC_CTEST_SKIP 77

static char g_tmp_h265[256];

static int has_node(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int hardware_available(void)
{
    rkvc_caps caps;

    if (getenv("RKVC_SKIP_HARDWARE_TESTS"))
        return 0;
    if (rkvc_query_caps(&caps) != RKVC_OK)
        return 0;
    if (!caps.has_rkmpp_enc || !caps.has_rkmpp_dec)
        return 0;
    if (!caps.has_dma_heap || !caps.has_rga)
        return 0;
    if (!has_node("/dev/mpp_service"))
        return 0;
    return 1;
}

static void make_temp_path(void)
{
    char path[] = "/tmp/rkvc-hw-test-XXXXXX.h265";
    int fd = mkstemps(path, 5);

    assert_true(fd >= 0);
    close(fd);
    unlink(path);

    snprintf(g_tmp_h265, sizeof(g_tmp_h265), "%s", path);
}

static void fill_nv12_frame(rkvc_frame *frame, int frame_index)
{
    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_info info;

    assert_int_equal(rkvc_frame_get_info(frame, &info), RKVC_OK);
    assert_int_equal(rkvc_frame_get_data(frame, planes, strides), RKVC_OK);
    assert_non_null(planes[0]);
    assert_non_null(planes[1]);

    for (int y = 0; y < info.height; y++) {
        for (int x = 0; x < info.width; x++)
            planes[0][y * strides[0] + x] = (uint8_t)((x + y + frame_index) & 0xff);
    }

    for (int y = 0; y < info.height / 2; y++) {
        for (int x = 0; x < info.width; x += 2) {
            planes[1][y * strides[1] + x] = 128;
            planes[1][y * strides[1] + x + 1] = (uint8_t)(96 + frame_index);
        }
    }
}

static void test_hardware_encode_decode_file_round_trip(void **state)
{
    (void)state;

    enum { WIDTH = 320, HEIGHT = 240, FRAMES = 8 };
    rkvc_encoder *enc = NULL;
    rkvc_decoder *dec = NULL;
    rkvc_encoder_config enc_cfg = rkvc_encoder_config_defaults();
    rkvc_decoder_config dec_cfg = rkvc_decoder_config_defaults();
    struct stat st;
    int decoded_frames = 0;

    make_temp_path();

    enc_cfg.width = WIDTH;
    enc_cfg.height = HEIGHT;
    enc_cfg.fps_num = 30;
    enc_cfg.fps_den = 1;
    enc_cfg.bitrate = 1000000;
    enc_cfg.gop_size = 30;
    enc_cfg.input_format = RKVC_PIX_FMT_NV12;

    assert_int_equal(rkvc_encoder_open_file(&enc, &enc_cfg, g_tmp_h265),
                     RKVC_OK);

    for (int i = 0; i < FRAMES; i++) {
        rkvc_frame *frame = NULL;
        rkvc_packet pkt;

        assert_int_equal(rkvc_frame_alloc(&frame, WIDTH, HEIGHT,
                                          RKVC_PIX_FMT_NV12),
                         RKVC_OK);
        fill_nv12_frame(frame, i);
        assert_int_equal(rkvc_frame_set_pts(frame, i), RKVC_OK);
        assert_int_equal(rkvc_encoder_send_frame(enc, frame), RKVC_OK);
        rkvc_frame_unref(frame);

        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
            assert_true(pkt.size >= 0);
        }
    }

    assert_int_equal(rkvc_encoder_close(enc), RKVC_OK);
    enc = NULL;

    assert_int_equal(stat(g_tmp_h265, &st), 0);
    assert_true(st.st_size > 0);

    assert_int_equal(rkvc_decoder_open_file(&dec, &dec_cfg, g_tmp_h265),
                     RKVC_OK);

    for (;;) {
        rkvc_err err = rkvc_decoder_read_packet(dec);
        if (err == RKVC_ERR_EOF)
            break;
        assert_int_equal(err, RKVC_OK);

        for (;;) {
            rkvc_frame *frame = NULL;
            err = rkvc_decoder_receive_frame(dec, &frame);
            if (err == RKVC_ERR_AGAIN || err == RKVC_ERR_EOF)
                break;
            assert_int_equal(err, RKVC_OK);
            assert_non_null(frame);
            decoded_frames++;
            rkvc_frame_unref(frame);
        }
    }

    for (;;) {
        rkvc_frame *frame = NULL;
        rkvc_err err = rkvc_decoder_receive_frame(dec, &frame);
        if (err == RKVC_ERR_AGAIN || err == RKVC_ERR_EOF)
            break;
        assert_int_equal(err, RKVC_OK);
        assert_non_null(frame);
        decoded_frames++;
        rkvc_frame_unref(frame);
    }

    assert_true(decoded_frames > 0);
    assert_int_equal(rkvc_decoder_close(dec), RKVC_OK);
    dec = NULL;

    unlink(g_tmp_h265);
    g_tmp_h265[0] = '\0';
}

static int cleanup(void **state)
{
    (void)state;

    if (g_tmp_h265[0] != '\0') {
        unlink(g_tmp_h265);
        g_tmp_h265[0] = '\0';
    }

    return 0;
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_teardown(test_hardware_encode_decode_file_round_trip,
                                  cleanup),
    };

    if (!hardware_available()) {
        fprintf(stderr, "RKVC hardware tests skipped: RKMPP devices unavailable\n");
        return RKVC_CTEST_SKIP;
    }

    return cmocka_run_group_tests(tests, NULL, NULL);
}
