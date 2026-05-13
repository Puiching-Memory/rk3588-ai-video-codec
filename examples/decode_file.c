/**
 * @file decode_file.c
 * @brief 示例: 离线 H.265 文件解码。
 *
 * 将 H.265 文件解码为原始帧，统计信息。
 *
 * 用法:
 *   decode_file input.h265 [-o output.nv12]
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
    const char *output = NULL;
    int verbose = 0;
    int c;

    static struct option long_opts[] = {
        {"output",  required_argument, 0, 'o'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "o:vh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'o': output = optarg; break;
        case 'v': verbose = 1; break;
        case 'h':
            printf("decode_file — 离线 H.265 解码示例\n"
                   "  输入文件作为位置参数\n"
                   "  -o FILE    输出 NV12 文件 (可选)\n"
                   "  -v         详细输出\n");
            return 0;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "用法: decode_file <input.h265> [-o output.nv12]\n");
        return 1;
    }
    input = argv[optind];

    rkvc_init();

    rkvc_decoder *dec = NULL;
    rkvc_decoder_config cfg = rkvc_decoder_config_defaults();

    rkvc_err err = rkvc_decoder_open_file(&dec, &cfg, input);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开解码器失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    int width = 0, height = 0, fps_num = 30, fps_den = 1;
    rkvc_decoder_get_video_info(dec, &width, &height, &fps_num, &fps_den);
    printf("解码 %s: %dx%d @ %d/%d fps\n", input, width, height, fps_num, fps_den);

    FILE *out_fp = NULL;
    if (output) {
        out_fp = fopen(output, "wb");
        if (!out_fp) {
            fprintf(stderr, "无法打开输出文件: %s\n", output);
            rkvc_decoder_close(dec);
            return 1;
        }
    }

    double t0 = now_sec();
    int frame_count = 0;
    uint64_t total_bytes = 0;

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
            frame_count++;

            if (out_fp) {
                uint8_t *planes[4] = {0};
                int strides[4] = {0};
                rkvc_frame_get_data(f, planes, strides);

                /* 写 Y 平面 */
                for (int y = 0; y < height; y++)
                    fwrite(planes[0] + y * strides[0], 1, width, out_fp);
                /* 写 UV 平面 */
                for (int y = 0; y < height / 2; y++)
                    fwrite(planes[1] + y * strides[1], 1, width, out_fp);
            }

            total_bytes += (uint64_t)width * height * 3 / 2;

            if (verbose && frame_count % 100 == 0)
                printf("  已解码 %d 帧\n", frame_count);

            rkvc_frame_unref(f);
        }
    }

    double elapsed = now_sec() - t0;

    if (out_fp)
        fclose(out_fp);

    printf("解码完成: %d 帧, %.3fs, %.1f fps\n",
           frame_count, elapsed,
           elapsed > 0 ? frame_count / elapsed : 0);
    printf("总像素数据: %.2f MB\n", total_bytes / (1024.0 * 1024.0));

    rkvc_decoder_close(dec);
    rkvc_deinit();
    return 0;
}
