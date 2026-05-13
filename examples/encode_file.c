/**
 * @file encode_file.c
 * @brief 示例: 离线 H.265 文件编码。
 *
 * 将原始 NV12 帧编码为 H.265 文件。
 *
 * 用法:
 *   encode_file -o output.h265 [-s 1920x1080] [-r 30] [-n 300] [-b 4000000]
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static int s_width  = 1920;
static int s_height = 1080;
static int s_fps    = 30;
static int s_frames = 300;
static int s_bitrate = 4000000;

static void generate_frame(rkvc_frame *f, int idx)
{
    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_get_data(f, planes, strides);

    /* Y: 条纹图案 */
    for (int y = 0; y < s_height; y++) {
        memset(planes[0] + y * strides[0],
               (uint8_t)((y + idx * 2) & 0xFF), s_width);
    }
    /* UV: 固定色度 */
    for (int y = 0; y < s_height / 2; y++) {
        for (int x = 0; x < strides[1]; x += 2) {
            planes[1][y * strides[1] + x]     = 128;
            planes[1][y * strides[1] + x + 1] = 128;
        }
    }

    rkvc_frame_set_pts(f, idx);
}

int main(int argc, char **argv)
{
    const char *output = "output.h265";
    int c;

    static struct option long_opts[] = {
        {"output",  required_argument, 0, 'o'},
        {"size",    required_argument, 0, 's'},
        {"rate",    required_argument, 0, 'r'},
        {"frames",  required_argument, 0, 'n'},
        {"bitrate", required_argument, 0, 'b'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "o:s:r:n:b:h", long_opts, NULL)) != -1) {
        switch (c) {
        case 'o': output = optarg; break;
        case 's': sscanf(optarg, "%dx%d", &s_width, &s_height); break;
        case 'r': s_fps = atoi(optarg); break;
        case 'n': s_frames = atoi(optarg); break;
        case 'b': s_bitrate = atoi(optarg); break;
        case 'h':
            printf("encode_file — 离线 H.265 编码示例\n"
                   "  -o FILE    输出文件 (默认 output.h265)\n"
                   "  -s WxH     分辨率 (默认 1920x1080)\n"
                   "  -r FPS     帧率 (默认 30)\n"
                   "  -n N       帧数 (默认 300)\n"
                   "  -b BPS     码率 (默认 4M)\n");
            return 0;
        }
    }

    rkvc_init();

    rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
    cfg.width   = s_width;
    cfg.height  = s_height;
    cfg.fps_num = s_fps;
    cfg.bitrate = s_bitrate;

    rkvc_encoder *enc = NULL;
    rkvc_err err = rkvc_encoder_open_file(&enc, &cfg, output);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开编码器失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    printf("编码 %dx%d@%d, %d 帧 → %s\n",
           s_width, s_height, s_fps, s_frames, output);

    for (int i = 0; i < s_frames; i++) {
        rkvc_frame *f = NULL;
        rkvc_frame_alloc(&f, s_width, s_height, RKVC_PIX_FMT_NV12);
        generate_frame(f, i);

        err = rkvc_encoder_send_frame(enc, f);
        rkvc_frame_unref(f);

        if (err != RKVC_OK && err != RKVC_ERR_AGAIN) {
            fprintf(stderr, "编码第 %d 帧失败: %s\n", i, rkvc_err_str(err));
            break;
        }

        rkvc_packet pkt;
        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
            /* 文件模式下自动写入 */
        }

        if ((i + 1) % 100 == 0)
            printf("  已编码 %d/%d 帧\n", i + 1, s_frames);
    }

    rkvc_encoder_close(enc);

    printf("编码完成: %s\n", output);
    rkvc_deinit();
    return 0;
}
