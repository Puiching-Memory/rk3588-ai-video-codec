/**
 * @file transcode.c
 * @brief 示例: 完整转码管线 (解码 → 编码)。
 *
 * 将 H.265 文件解码后重新编码，可调整分辨率/码率。
 *
 * 用法:
 *   transcode -i input.h265 -o output.h265 [-s WxH] [-b BPS]
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1.0e9;
}

int main(int argc, char **argv)
{
    const char *input  = NULL;
    const char *output = "transcode_out.h265";
    int out_width = 0, out_height = 0;
    int out_bitrate = 4000000;
    int c;

    static struct option long_opts[] = {
        {"input",   required_argument, 0, 'i'},
        {"output",  required_argument, 0, 'o'},
        {"size",    required_argument, 0, 's'},
        {"bitrate", required_argument, 0, 'b'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "i:o:s:b:h", long_opts, NULL)) != -1) {
        switch (c) {
        case 'i': input = optarg; break;
        case 'o': output = optarg; break;
        case 's': sscanf(optarg, "%dx%d", &out_width, &out_height); break;
        case 'b': out_bitrate = atoi(optarg); break;
        case 'h':
            printf("transcode — 转码示例\n"
                   "  -i FILE    输入文件\n"
                   "  -o FILE    输出文件\n"
                   "  -s WxH     输出分辨率 (默认保持原始)\n"
                   "  -b BPS     输出码率 (默认 4M)\n");
            return 0;
        }
    }

    if (!input) {
        fprintf(stderr, "需要指定输入文件: -i <file>\n");
        return 1;
    }

    rkvc_init();

    /* 打开解码器 */
    rkvc_decoder *dec = NULL;
    rkvc_decoder_config dcfg = rkvc_decoder_config_defaults();

    rkvc_err err = rkvc_decoder_open_file(&dec, &dcfg, input);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开解码器失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    int src_w = 0, src_h = 0, fps_num = 30, fps_den = 1;
    rkvc_decoder_get_video_info(dec, &src_w, &src_h, &fps_num, &fps_den);

    /* 如果未指定输出分辨率，使用源分辨率 */
    if (out_width <= 0)  out_width  = src_w;
    if (out_height <= 0) out_height = src_h;

    printf("转码: %s (%dx%d) → %s (%dx%d @ %d kbps)\n",
           input, src_w, src_h, output, out_width, out_height,
           out_bitrate / 1000);

    /* 打开编码器 */
    rkvc_encoder *enc = NULL;
    rkvc_encoder_config ecfg = rkvc_encoder_config_defaults();
    ecfg.width   = out_width;
    ecfg.height  = out_height;
    ecfg.fps_num = fps_num;
    ecfg.fps_den = fps_den;
    ecfg.bitrate = out_bitrate;

    err = rkvc_encoder_open_file(&enc, &ecfg, output);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开编码器失败: %s\n", rkvc_err_str(err));
        rkvc_decoder_close(dec);
        return 1;
    }

    double t0 = now_sec();
    int frame_count = 0;

    for (;;) {
        err = rkvc_decoder_read_packet(dec);
        if (err == RKVC_ERR_EOF)
            break;
        if (err != RKVC_OK) {
            fprintf(stderr, "读取包失败: %s\n", rkvc_err_str(err));
            break;
        }

        rkvc_frame *f = NULL;
        while (rkvc_decoder_receive_frame(dec, &f) == RKVC_OK) {
            /* 简单转码: 直接送入编码器 (同分辨率时零拷贝) */
            err = rkvc_encoder_send_frame(enc, f);
            rkvc_frame_unref(f);

            if (err != RKVC_OK && err != RKVC_ERR_AGAIN) {
                fprintf(stderr, "编码失败: %s\n", rkvc_err_str(err));
                break;
            }

            rkvc_packet pkt;
            while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
                /* 文件模式自动写入 */
            }

            frame_count++;
            if (frame_count % 100 == 0)
                printf("  已转码 %d 帧\n", frame_count);
        }
    }

    /* flush */
    rkvc_encoder_drain(enc);
    rkvc_packet pkt;
    while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {}

    double elapsed = now_sec() - t0;

    printf("\n转码完成:\n");
    printf("  帧数: %d\n", frame_count);
    printf("  耗时: %.3fs\n", elapsed);
    printf("  帧率: %.1f fps\n",
           elapsed > 0 ? frame_count / elapsed : 0);

    rkvc_encoder_close(enc);
    rkvc_decoder_close(dec);
    rkvc_deinit();
    return 0;
}
