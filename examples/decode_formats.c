/**
 * @file decode_formats.c
 * @brief 演示: 用不同 output_format 解码同一 H.265 流，验证帧格式。
 *
 * 编码一段测试 H.265 流，然后分别以 NV12 / YUV420P / NV16 / P010
 * 作为 output_format 解码，打印每帧实际格式。
 *
 * 用法: ./decode_formats
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *fmt_name(rkvc_pix_fmt f)
{
    switch (f) {
    case RKVC_PIX_FMT_NV12:    return "NV12";
    case RKVC_PIX_FMT_YUV420P: return "YUV420P";
    case RKVC_PIX_FMT_NV16:    return "NV16";
    case RKVC_PIX_FMT_P010:    return "P010";
    default:                   return "UNKNOWN";
    }
}

/* 用 NV12 测试图案编码一段短 H.265 流 */
static int encode_test_stream(const char *path, int width, int height,
                              int frames)
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

    if (rkvc_encoder_open_file(&enc, &cfg, path) != RKVC_OK)
        return -1;

    for (int i = 0; i < frames; i++) {
        rkvc_frame *frame = NULL;
        rkvc_packet pkt;

        if (rkvc_frame_alloc(&frame, width, height, RKVC_PIX_FMT_NV12)
            != RKVC_OK)
            goto fail;

        uint8_t *planes[4] = {0};
        int strides[4] = {0};
        rkvc_frame_get_data(frame, planes, strides);
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                planes[0][y * strides[0] + x] = (uint8_t)((x + y + i) & 0xff);
        for (int y = 0; y < height / 2; y++)
            for (int x = 0; x < width; x += 2) {
                planes[1][y * strides[1] + x]     = 128;
                planes[1][y * strides[1] + x + 1] = (uint8_t)(96 + i);
            }

        rkvc_frame_set_pts(frame, i);
        if (rkvc_encoder_send_frame(enc, frame) != RKVC_OK) {
            rkvc_frame_unref(frame);
            goto fail;
        }
        rkvc_frame_unref(frame);
        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK)
            ;
    }

    rkvc_encoder_close(enc);
    return 0;
fail:
    rkvc_encoder_close(enc);
    return -1;
}

/* 以指定 output_format 解码，返回解码帧数；打印前几帧的实际格式 */
static int decode_with_format(const char *path, rkvc_pix_fmt fmt)
{
    rkvc_decoder *dec = NULL;
    rkvc_decoder_config cfg = rkvc_decoder_config_defaults();
    cfg.output_format = fmt;

    if (rkvc_decoder_open_file(&dec, &cfg, path) != RKVC_OK) {
        printf("  打开解码器失败\n");
        return -1;
    }

    int count = 0;
    int format_ok = 1;
    for (;;) {
        rkvc_err err = rkvc_decoder_read_packet(dec);
        if (err == RKVC_ERR_EOF)
            break;
        if (err != RKVC_OK)
            continue;

        rkvc_frame *f = NULL;
        while (rkvc_decoder_receive_frame(dec, &f) == RKVC_OK) {
            rkvc_frame_info info;
            rkvc_frame_get_info(f, &info);

            if (count < 2)
                printf("  帧 %d: format=%s (期望 %s) %s\n",
                       count, fmt_name(info.format), fmt_name(fmt),
                       info.format == fmt ? "✓" : "✗ MISMATCH");

            if (info.format != fmt)
                format_ok = 0;

            count++;
            rkvc_frame_unref(f);
        }
    }

    /* drain */
    for (;;) {
        rkvc_frame *f = NULL;
        rkvc_err err = rkvc_decoder_receive_frame(dec, &f);
        if (err == RKVC_ERR_AGAIN || err == RKVC_ERR_EOF)
            break;
        if (err != RKVC_OK)
            break;
        rkvc_frame_info info;
        rkvc_frame_get_info(f, &info);
        if (info.format != fmt)
            format_ok = 0;
        count++;
        rkvc_frame_unref(f);
    }

    rkvc_decoder_close(dec);

    printf("  共 %d 帧, 格式全部匹配: %s\n", count,
           format_ok ? "是 ✓" : "否 ✗");
    return format_ok ? count : -1;
}

int main(void)
{
    const char *path = "/tmp/rkvc-fmt-demo.h265";
    enum { WIDTH = 320, HEIGHT = 240, FRAMES = 8 };

    rkvc_init();

    printf("=== 编码测试流 (%dx%d, %d 帧) ===\n", WIDTH, HEIGHT, FRAMES);
    if (encode_test_stream(path, WIDTH, HEIGHT, FRAMES) != 0) {
        fprintf(stderr, "编码失败\n");
        return 1;
    }
    printf("已生成: %s\n\n", path);

    rkvc_pix_fmt formats[] = {
        RKVC_PIX_FMT_NV12,
        RKVC_PIX_FMT_YUV420P,
        RKVC_PIX_FMT_NV16,
        RKVC_PIX_FMT_P010,
    };

    int all_ok = 1;
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        printf("=== 以 %s 格式解码 ===\n", fmt_name(formats[i]));
        int n = decode_with_format(path, formats[i]);
        if (n < 0)
            all_ok = 0;
        printf("\n");
    }

    unlink(path);
    rkvc_deinit();

    printf("=== 总结 ===\n");
    printf("所有格式输出正确: %s\n", all_ok ? "是 ✓" : "否 ✗");
    return all_ok ? 0 : 1;
}
