/**
 * @file rkvc_encode.c
 * @brief 命令行 H.265 编码工具。
 *
 * 用法:
 *   rkvc_encode -i input.nv12 -o output.h265 -s 1920x1080 -r 30 -b 4M
 *   rkvc_encode -o output.h265 --testsrc -s 1920x1080 -n 300
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static void usage(void) {
    printf(
        "rkvc_encode — H.265 RKMPP 硬件编码工具\n"
        "\n"
        "用法:\n"
        "  rkvc_encode -o OUTPUT [选项]\n"
        "\n"
        "选项:\n"
        "  -o, --output FILE     输出文件 (必填, .h265/.mp4/.mkv)\n"
        "  -i, --input FILE      输入原始 NV12 文件\n"
        "  --testsrc             使用测试图案 (替代 -i)\n"
        "  -s, --size WxH        分辨率 (默认 1920x1080)\n"
        "  -r, --rate FPS        帧率 (默认 30)\n"
        "  -n, --frames N        帧数 (默认 300, 仅 --testsrc)\n"
        "  -b, --bitrate BPS     码率 (默认 4000000)\n"
        "  -p, --preset PRESET   编码预设: fast/medium/slow (默认 medium)\n"
        "  -v, --verbose         详细输出\n"
        "  -h, --help            显示帮助\n"
    );
}

static int generate_test_frame(rkvc_frame *f, int idx, int w, int h) {
    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_get_data(f, planes, strides);

    if (planes[0])
        for (int y = 0; y < h; y++)
            memset(planes[0] + y * strides[0], (uint8_t)((y + idx * 2) & 0xFF), w);
    if (planes[1])
        for (int y = 0; y < h / 2; y++)
            memset(planes[1] + y * strides[1], 128, strides[1]);

    rkvc_frame_set_pts(f, idx);
    return 0;
}

int main(int argc, char **argv) {
    const char *output = NULL;
    const char *input = NULL;
    int testsrc = 0, width = 1920, height = 1080, fps = 30, frames = 300;
    int64_t bitrate = 4000000;
    rkvc_preset preset = RKVC_PRESET_MEDIUM;
    int verbose = 0;

    static struct option long_opts[] = {
        {"output",  required_argument, 0, 'o'},
        {"input",   required_argument, 0, 'i'},
        {"testsrc", no_argument,       0, 'T'},
        {"size",    required_argument, 0, 's'},
        {"rate",    required_argument, 0, 'r'},
        {"frames",  required_argument, 0, 'n'},
        {"bitrate", required_argument, 0, 'b'},
        {"preset",  required_argument, 0, 'p'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "o:i:s:r:n:b:p:vh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'o': output = optarg; break;
        case 'i': input = optarg; break;
        case 'T': testsrc = 1; break;
        case 's': sscanf(optarg, "%dx%d", &width, &height); break;
        case 'r': fps = atoi(optarg); break;
        case 'n': frames = atoi(optarg); break;
        case 'b': bitrate = atoll(optarg); break;
        case 'p':
            if (strcmp(optarg, "fast") == 0) preset = RKVC_PRESET_FAST;
            else if (strcmp(optarg, "slow") == 0) preset = RKVC_PRESET_SLOW;
            break;
        case 'v': verbose = 1; break;
        case 'h': usage(); return 0;
        default: usage(); return 1;
        }
    }

    if (!output) { usage(); return 1; }
    if (!input && !testsrc) { fprintf(stderr, "错误: 需要 -i 或 --testsrc\n"); return 1; }

    rkvc_init();

    rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
    cfg.width = width; cfg.height = height;
    cfg.fps_num = fps; cfg.bitrate = bitrate;
    cfg.preset = preset;

    rkvc_encoder *enc = NULL;
    rkvc_err err = rkvc_encoder_open_file(&enc, &cfg, output);
    if (err != RKVC_OK) {
        fprintf(stderr, "编码器打开失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    if (verbose)
        fprintf(stderr, "编码 %dx%d@%d, %s → %s\n", width, height, fps,
                testsrc ? "testsrc" : input, output);

    FILE *in_fp = NULL;
    if (input) {
        in_fp = fopen(input, "rb");
        if (!in_fp) { perror("打开输入文件失败"); return 1; }
    }

    int frame_count = 0;
    size_t nv12_size = (size_t)width * height * 3 / 2;

    for (int i = 0; testsrc ? (i < frames) : 1; i++) {
        rkvc_frame *f = NULL;
        rkvc_frame_alloc(&f, width, height, RKVC_PIX_FMT_NV12);

        if (testsrc) {
            generate_test_frame(f, i, width, height);
        } else {
            uint8_t *planes[4] = {0};
            int strides[4] = {0};
            rkvc_frame_get_data(f, planes, strides);
            if (strides[0] == width) {
                if (fread(planes[0], 1, nv12_size, in_fp) != nv12_size) {
                    rkvc_frame_unref(f);
                    break;
                }
            } else {
                for (int y = 0; y < height * 3 / 2; y++)
                    fread(planes[0] + y * strides[0], 1, width, in_fp);
            }
            rkvc_frame_set_pts(f, i);
        }

        err = rkvc_encoder_send_frame(enc, f);
        rkvc_frame_unref(f);

        if (err != RKVC_OK && err != RKVC_ERR_AGAIN)
            break;

        rkvc_packet pkt;
        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {}

        frame_count++;
        if (verbose && frame_count % 100 == 0)
            fprintf(stderr, "  已编码 %d 帧\n", frame_count);
    }

    rkvc_encoder_close(enc);
    if (in_fp) fclose(in_fp);

    fprintf(stderr, "编码完成: %d 帧 → %s\n", frame_count, output);
    rkvc_deinit();
    return 0;
}
