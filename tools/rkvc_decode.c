/**
 * @file rkvc_decode.c
 * @brief 命令行 H.265 解码工具，支持文件/stdin/管道。
 *
 * 用法:
 *   rkvc_decode -i input.h265 -o output.nv12
 *   rkvc_decode --stdin -o output.nv12 < input.h265
 *   rkvc_encode --stdin --stdout -s 1920x1080 < in.nv12 | rkvc_decode --stdin -o out.nv12
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
        "输入源 (二选一):\n"
        "  -i, --input FILE      输入 H.265 文件\n"
        "  --stdin               从 stdin 读取 H.265 流\n"
        "\n"
        "输出:\n"
        "  -o, --output FILE     输出 NV12 文件 (可选)\n"
        "  --stdout              输出到 stdout (用于管道)\n"
        "\n"
        "选项:\n"
        "  -s, --size WxH        分辨率 (stdin 模式必填)\n"
        "  -o, --output FILE     输出 NV12 文件 (可选)\n"
        "  --stdout              输出到 stdout (用于管道)\n"
        "  -v, --verbose         详细输出到 stderr\n"
        "  -h, --help            显示帮助\n"
        "\n"
        "示例:\n"
        "  # 离线解码\n"
        "  rkvc_decode -i input.h265 -o output.nv12\n"
        "\n"
        "  # 管道: 编码 → 解码\n"
        "  rkvc_encode --stdin --stdout -s 1920x1080 < raw.nv12 | \\\n"
        "    rkvc_decode --stdin -o decoded.nv12\n"
    );
}

int main(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    int use_stdin = 0, use_stdout = 0;
    int width = 0, height = 0;
    int verbose = 0;

    static struct option long_opts[] = {
        {"input",   required_argument, 0, 'i'},
        {"output",  required_argument, 0, 'o'},
        {"size",    required_argument, 0, 's'},
        {"stdin",   no_argument,       0, 'I'},
        {"stdout",  no_argument,       0, 'O'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "i:o:s:vh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'i': input = optarg; break;
        case 'o': output = optarg; break;
        case 's': sscanf(optarg, "%dx%d", &width, &height); break;
        case 'I': use_stdin = 1; break;
        case 'O': use_stdout = 1; break;
        case 'v': verbose = 1; break;
        case 'h': usage(); return 0;
        default: usage(); return 1;
        }
    }

    if (!input && !use_stdin) { usage(); return 1; }
    if (use_stdin && (width <= 0 || height <= 0)) {
        fprintf(stderr, "错误: stdin 模式需要 -s WxH 指定分辨率\n");
        return 1;
    }

    rkvc_init();

    rkvc_decoder *dec = NULL;
    rkvc_decoder_config cfg = rkvc_decoder_config_defaults();
    rkvc_err err;

    if (use_stdin) {
        /* stdin 模式: 用 demuxer 从 stdin 读取 */
        err = rkvc_decoder_open_file(&dec, &cfg, "pipe:0");
    } else {
        err = rkvc_decoder_open_file(&dec, &cfg, input);
    }

    if (err != RKVC_OK) {
        fprintf(stderr, "解码器打开失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    int w = 0, h = 0, fps_num = 30, fps_den = 1;
    rkvc_decoder_get_video_info(dec, &w, &h, &fps_num, &fps_den);

    /* stdin 模式使用命令行指定的分辨率 */
    if (use_stdin && width > 0) { w = width; h = height; }

    if (verbose)
        fprintf(stderr, "解码 %s: %dx%d @ %d/%d fps → %s\n",
                use_stdin ? "stdin" : input, w, h, fps_num, fps_den,
                use_stdout ? "stdout" : (output ? output : "null"));

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

            if (out_fp || use_stdout) {
                uint8_t *planes[4] = {0};
                int strides[4] = {0};
                rkvc_frame_get_data(f, planes, strides);
                FILE *dst = use_stdout ? stdout : out_fp;
                for (int y = 0; y < h; y++)
                    fwrite(planes[0] + y * strides[0], 1, w, dst);
                for (int y = 0; y < h / 2; y++)
                    fwrite(planes[1] + y * strides[1], 1, w, dst);
            }

            rkvc_frame_unref(f);
        }

        if (verbose && frame_count % 100 == 0)
            fprintf(stderr, "  已解码 %d 帧\n", frame_count);
    }

    double elapsed = now_sec() - t0;
    if (out_fp) fclose(out_fp);
    if (use_stdout) fflush(stdout);

    fprintf(stderr, "解码完成: %d 帧, %.3fs, %.1f fps\n",
            frame_count, elapsed,
            elapsed > 0 ? frame_count / elapsed : 0);

    rkvc_decoder_close(dec);
    rkvc_deinit();
    return 0;
}
