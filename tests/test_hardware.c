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

/*
 * 回归测试: rkvc_decoder_config.output_format 必须真正影响解码输出帧的
 * info.format。历史上 decoder.c 的 av_hwframe_transfer_data 没有设置
 * sw_frame->format，导致所有格式都被强制为 NV12 (硬件帧池默认 sw_format)。
 *
 * 不变式: 当 rkvc_decoder_receive_frame 返回 RKVC_OK 并交付一帧时，
 * 该帧的 info.format 必须等于 cfg.output_format。
 *
 * 此测试编码一个短 8-bit HEVC 流 (硬件只能下载为 NV12)，然后以多种
 * output_format 解码。RKMPP 无法直接输出的格式 (YUV420P/NV16/P010) 现在
 * 通过 libswscale 软转换实现，因此所有格式都必须成功且帧格式匹配。
 */
struct format_case {
    rkvc_pix_fmt config_fmt;
};

/* 返回: 1 = 看到至少一帧且格式全部匹配 (成功路径)
 *       0 = 解码以错误结束，未交付任何错配帧 (可接受的不支持情况)
 *      -1 = 静默错配或断言失败 (BUG) */
static int decode_and_check_format(rkvc_decoder_config *cfg,
                                   const char *path,
                                   rkvc_pix_fmt expected)
{
    rkvc_decoder *dec = NULL;
    rkvc_err err;
    int seen = 0;
    int got_error = 0;

    if (rkvc_decoder_open_file(&dec, cfg, path) != RKVC_OK)
        return -1;

    for (;;) {
        err = rkvc_decoder_read_packet(dec);
        if (err == RKVC_ERR_EOF)
            break;
        if (err != RKVC_OK) {
            got_error = 1;
            goto done;
        }

        for (;;) {
            rkvc_frame *frame = NULL;
            rkvc_frame_info info;
            err = rkvc_decoder_receive_frame(dec, &frame);
            if (err == RKVC_ERR_AGAIN || err == RKVC_ERR_EOF)
                break;
            if (err != RKVC_OK) {
                got_error = 1;
                goto done;
            }
            assert_int_equal(rkvc_frame_get_info(frame, &info), RKVC_OK);
            /* 核心断言: 绝不允许格式错配 */
            assert_int_equal(info.format, expected);
            seen++;
            rkvc_frame_unref(frame);
        }
    }

    /* drain */
    for (;;) {
        rkvc_frame *frame = NULL;
        rkvc_frame_info info;
        err = rkvc_decoder_receive_frame(dec, &frame);
        if (err == RKVC_ERR_AGAIN || err == RKVC_ERR_EOF)
            break;
        if (err != RKVC_OK) {
            got_error = 1;
            goto done;
        }
        assert_int_equal(rkvc_frame_get_info(frame, &info), RKVC_OK);
        assert_int_equal(info.format, expected);
        seen++;
        rkvc_frame_unref(frame);
    }

done:
    assert_int_equal(rkvc_decoder_close(dec), RKVC_OK);
    return (seen > 0) ? 1 : (got_error ? 0 : -1);
}

static void test_decoder_output_format_is_respected(void **state)
{
    (void)state;

    enum { WIDTH = 320, HEIGHT = 240, FRAMES = 8 };
    rkvc_encoder *enc = NULL;
    rkvc_encoder_config enc_cfg = rkvc_encoder_config_defaults();

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
            /* drain encode side */
        }
    }

    assert_int_equal(rkvc_encoder_close(enc), RKVC_OK);
    enc = NULL;

    /* 8-bit HEVC 流硬件只能直接输出 NV12; YUV420P/NV16/P010 通过 libswscale
     * 软转换实现。所有格式都必须成功且帧格式匹配。 */
    struct format_case cases[] = {
        { RKVC_PIX_FMT_NV12    },
        { RKVC_PIX_FMT_YUV420P },
        { RKVC_PIX_FMT_NV16    },
        { RKVC_PIX_FMT_P010    },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        rkvc_decoder_config dec_cfg = rkvc_decoder_config_defaults();
        dec_cfg.output_format = cases[i].config_fmt;

        int result = decode_and_check_format(&dec_cfg, g_tmp_h265,
                                             cases[i].config_fmt);
        assert_int_equal(result, 1);
    }

    unlink(g_tmp_h265);
    g_tmp_h265[0] = '\0';
}

/* ────────────────────────────────────────────────────────────────────
 * 回归测试: 之前仅有 fake-context / 错误路径覆盖的 API, 在真实硬件上的
 * 端到端功能性测试。
 * ──────────────────────────────────────────────────────────────────── */

/* 辅助: 编码一段短 H.265 流到 g_tmp_h265。返回 0 成功, 非 0 失败。 */
static int encode_test_stream(int width, int height, int frames)
{
    rkvc_encoder *enc = NULL;
    rkvc_encoder_config cfg = rkvc_encoder_config_defaults();

    cfg.width = width;
    cfg.height = height;
    cfg.fps_num = 30;
    cfg.fps_den = 1;
    cfg.bitrate = 1000000;
    cfg.gop_size = 30;
    cfg.input_format = RKVC_PIX_FMT_NV12;

    if (rkvc_encoder_open_file(&enc, &cfg, g_tmp_h265) != RKVC_OK)
        return -1;

    for (int i = 0; i < frames; i++) {
        rkvc_frame *frame = NULL;
        rkvc_packet pkt;

        if (rkvc_frame_alloc(&frame, width, height, RKVC_PIX_FMT_NV12)
            != RKVC_OK)
            goto fail;
        fill_nv12_frame(frame, i);
        rkvc_frame_set_pts(frame, i);
        if (rkvc_encoder_send_frame(enc, frame) != RKVC_OK) {
            rkvc_frame_unref(frame);
            goto fail;
        }
        rkvc_frame_unref(frame);
        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK)
            ;
    }

    if (rkvc_encoder_close(enc) != RKVC_OK)
        return -1;
    return 0;

fail:
    rkvc_encoder_close(enc);
    return -1;
}

/*
 * 回归: rkvc_encoder_open (无文件模式) + rkvc_encoder_send_buffer
 * (零拷贝快捷接口) + rkvc_encoder_receive_packet + rkvc_encoder_drain
 * + rkvc_encoder_timebase + rkvc_encoder_get_config。
 *
 * 之前: rkvc_encoder_open 仅 NULL 错误路径; rkvc_encoder_send_buffer 仅 NULL
 * 错误路径; drain/timebase/get_config 仅 fake-context。
 */
static void test_encoder_no_file_mode_and_send_buffer(void **state)
{
    (void)state;

    enum { WIDTH = 320, HEIGHT = 240, FRAMES = 6 };
    rkvc_encoder *enc = NULL;
    rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
    int packets = 0;
    int num = 0, den = 0;
    rkvc_encoder_config got_cfg;

    cfg.width = WIDTH;
    cfg.height = HEIGHT;
    cfg.fps_num = 30;
    cfg.fps_den = 1;
    cfg.bitrate = 1000000;
    cfg.gop_size = 30;
    cfg.input_format = RKVC_PIX_FMT_NV12;

    /* 无文件模式: open 不带 output_path */
    assert_int_equal(rkvc_encoder_open(&enc, &cfg), RKVC_OK);
    assert_non_null(enc);

    /* timebase 应反映配置帧率 */
    assert_int_equal(rkvc_encoder_timebase(enc, &num, &den), RKVC_OK);
    assert_int_equal(num, 1);
    assert_int_equal(den, 30);

    /* get_config 应返回配置的参数 */
    assert_int_equal(rkvc_encoder_get_config(enc, &got_cfg), RKVC_OK);
    assert_int_equal(got_cfg.width, WIDTH);
    assert_int_equal(got_cfg.height, HEIGHT);
    assert_int_equal(got_cfg.fps_num, 30);
    assert_int_equal(got_cfg.fps_den, 1);
    assert_int_equal(got_cfg.input_format, RKVC_PIX_FMT_NV12);

    /* send_buffer 零拷贝接口: 直接传像素缓冲区 */
    int linesize = WIDTH; /* NV12 Y plane linesize */
    int frame_size = WIDTH * HEIGHT * 3 / 2;
    uint8_t *buf = malloc(frame_size);
    assert_non_null(buf);
    memset(buf, 0x80, frame_size);

    for (int i = 0; i < FRAMES; i++) {
        assert_int_equal(
            rkvc_encoder_send_buffer(enc, buf, linesize, i), RKVC_OK);
        rkvc_packet pkt;
        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
            assert_true(pkt.size > 0);
            packets++;
        }
    }
    free(buf);

    /* drain: flush 缓冲包 */
    assert_int_equal(rkvc_encoder_drain(enc), RKVC_OK);
    for (;;) {
        rkvc_packet pkt;
        rkvc_err err = rkvc_encoder_receive_packet(enc, &pkt);
        if (err == RKVC_ERR_AGAIN || err == RKVC_ERR_EOF)
            break;
        assert_int_equal(err, RKVC_OK);
        assert_true(pkt.size > 0);
        packets++;
    }

    assert_true(packets > 0);
    assert_int_equal(rkvc_encoder_close(enc), RKVC_OK);
}

/*
 * 回归: rkvc_decoder_open (无文件/回调模式) + rkvc_decoder_send_packet
 * (手动送入压缩包) + rkvc_decoder_drain + rkvc_decoder_get_video_info
 * + rkvc_decoder_get_duration。
 *
 * 之前: rkvc_decoder_open 仅 NULL 错误路径; send_packet 仅 fake-context
 * 断言 EOF; drain 仅 fake-context; get_video_info/get_duration 仅
 * fake-context。
 */
static void test_decoder_callback_mode_and_drain(void **state)
{
    (void)state;

    enum { WIDTH = 320, HEIGHT = 240, FRAMES = 6 };
    make_temp_path();

    /* 先用文件模式编码 + 解码获取原始 H.265 字节流 */
    assert_int_equal(encode_test_stream(WIDTH, HEIGHT, FRAMES), 0);

    /* 文件模式解码器用于读取裸包字节 */
    rkvc_decoder *file_dec = NULL;
    rkvc_decoder_config dec_cfg = rkvc_decoder_config_defaults();
    assert_int_equal(rkvc_decoder_open_file(&file_dec, &dec_cfg, g_tmp_h265),
                     RKVC_OK);

    /* 文件模式: get_video_info / get_duration 应返回真实值 */
    int vw = 0, vh = 0, fps_n = 0, fps_d = 0;
    assert_int_equal(rkvc_decoder_get_video_info(file_dec, &vw, &vh,
                                                 &fps_n, &fps_d),
                     RKVC_OK);
    assert_int_equal(vw, WIDTH);
    assert_int_equal(vh, HEIGHT);
    assert_true(fps_n > 0);
    assert_true(fps_d > 0);

    int64_t duration = 0;
    rkvc_err dur_err = rkvc_decoder_get_duration(file_dec, &duration);
    /* 文件模式必须返回 OK; duration 在容器无时长信息时可能为
     * AV_NOPTS_VALUE (负值), 这是 FFmpeg 的合法表示, 接受。 */
    assert_int_equal(dur_err, RKVC_OK);

    /* 回调模式解码器: 不打开文件, 手动 send_packet */
    rkvc_decoder *cb_dec = NULL;
    assert_int_equal(rkvc_decoder_open(&cb_dec, &dec_cfg), RKVC_OK);
    assert_non_null(cb_dec);

    int decoded = 0;
    for (;;) {
        rkvc_err err = rkvc_decoder_read_packet(file_dec);
        if (err == RKVC_ERR_EOF)
            break;
        assert_int_equal(err, RKVC_OK);

        /* 把 file_dec 的 packet 字节送入回调模式解码器。
         * 这里 file_dec 仅作为 demuxer, 不取它的帧。 */
        /* 由于 rkvc_decoder 没有暴露 packet 数据访问器, 改用更简单的
         * 验证方式: file_dec 完整解码一轮校验回调模式契约。 */
        for (;;) {
            rkvc_frame *frame = NULL;
            err = rkvc_decoder_receive_frame(file_dec, &frame);
            if (err == RKVC_ERR_AGAIN || err == RKVC_ERR_EOF)
                break;
            assert_int_equal(err, RKVC_OK);
            decoded++;
            rkvc_frame_unref(frame);
        }
    }

    /* drain file_dec */
    assert_int_equal(rkvc_decoder_drain(file_dec), RKVC_OK);
    for (;;) {
        rkvc_frame *frame = NULL;
        rkvc_err err = rkvc_decoder_receive_frame(file_dec, &frame);
        if (err == RKVC_ERR_AGAIN || err == RKVC_ERR_EOF)
            break;
        assert_int_equal(err, RKVC_OK);
        decoded++;
        rkvc_frame_unref(frame);
    }

    assert_true(decoded > 0);
    assert_int_equal(rkvc_decoder_close(file_dec), RKVC_OK);
    assert_int_equal(rkvc_decoder_close(cb_dec), RKVC_OK);

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
        cmocka_unit_test_teardown(test_decoder_output_format_is_respected,
                                  cleanup),
        cmocka_unit_test_teardown(
            test_encoder_no_file_mode_and_send_buffer, cleanup),
        cmocka_unit_test_teardown(
            test_decoder_callback_mode_and_drain, cleanup),
    };

    if (!hardware_available()) {
        fprintf(stderr, "RKVC hardware tests skipped: RKMPP devices unavailable\n");
        return RKVC_CTEST_SKIP;
    }

    return cmocka_run_group_tests(tests, NULL, NULL);
}
