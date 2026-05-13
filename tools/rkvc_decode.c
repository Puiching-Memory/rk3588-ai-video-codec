/**
 * @file rkvc_decode.c
 * @brief 命令行 H.265 解码工具。
 *
 * 用法:
 *   rkvc_decode -i input.h265 [-o output.nv12] [-v]
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1.0e9;
}

static void usage(void) {
    printf(
        "rkvc_decode — H.265 RKMPP 硬件解码工具\n"
        "\n"
        "用法:\n"
        "  rkvc_decode -i INPUT [选项]\n"
        "\n"
        "选项:\n"
        "  -i, --input FILE      输入文件 (必填)\n"
        "  -o, --output FILE     输出 NV12 文件 (可选)\n"
        "  -v, --verbose         详细输出\n"
        "  -h, --help            显示帮助\n"
    );
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    int verbose = 0;

    static struct option long_opts[] = {
        {"input",   required_argument, 0, 'i'},
        {"output",  required_argument, 0, 'o'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "i:o:vh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'i': input = optarg; break;
        case 'o': output = optarg; break;
        case 'v': verbose = 1; break;
        case 'h': usage(); return 0;
        default: usage(); return 1;
        }
    }

    if (!input) { usage(); return 1; }

    rkvc_init();

    rkvc_decoder *dec = NULL;
    rkvc_decoder_config cfg = rkvc_decoder_config_defaults();
    rkvc_err err = rkvc_decoder_open_file(&dec, &cfg, input);
    if (err != RKVC_OK) {
        fprintf(stderr, "解码器打开失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    int w = 0, h = 0, fps_num = 30, fps_den = 1;
    rkvc_decoder_get_video_info(dec, &w, &h, &fps_num, &fps_den);
    fprintf(stderr, "解码 %s: %dx%d @ %d/%d fps\n", input, w, h, fps_num, fps_den);

    FILE *out_fp = NULL;
    if (output) {
        out_fp = fopen(output, "wb");
        if (!out_fp) { perror("打开输出文件失败"); return 1; }
    }

    double t0 = now_sec();
    int frame_count = 0;

    for (;;) {
        err = rkvc_decoder_read_packet(dec);
        if (err == RKVC_ERR_EOF) break;
        if (err != RKVC_OK) break;

        rkvc_frame *f = NULL;
        while (rkvc_decoder_receive_frame(dec, &f) == RKVC_OK) {
            frame_count++;

            if (out_fp) {
                uint8_t *planes[4] = {0};
                int strides[4] = {0};
                rkvc_frame_get_data(f, planes, strides);
                for (int y = 0; y < h; y++)
                    fwrite(planes[0] + y * strides[0], 1, w, out_fp);
                for (int y = 0; y < h / 2; y++)
                    fwrite(planes[1] + y * strides[1], 1, w, out_fp);
            }

            rkvc_frame_unref(f);

            if (verbose && frame_count % 100 == 0)
                fprintf(stderr, "  已解码 %d 帧\n", frame_count);
        }
    }

    double elapsed = now_sec() - t0;
    if (out_fp) fclose(out_fp);

    fprintf(stderr, "解码完成: %d 帧, %.3fs, %.1f fps\n",
            frame_count, elapsed,
            elapsed > 0 ? frame_count / elapsed : 0);

    rkvc_decoder_close(dec);
    rkvc_deinit();
    return 0;
}
