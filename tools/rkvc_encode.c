/**
 * @file rkvc_encode.c
 * @brief 命令行 H.265 编码工具，支持文件/stdin/管道。
 *
 * 用法:
 *   rkvc_encode -i input.nv12 -o output.h265 -s 1920x1080
 *   rkvc_encode --testsrc -o output.h265 -n 300
 *   cat input.nv12 | rkvc_encode --stdin -o output.h265 -s 1920x1080
 *   rkvc_encode --stdin --stdout -s 1920x1080 < in.nv12 > out.h265
 *   rkvc_encode --stdin --stdout -s 1920x1080 | rkvc_decode --stdin -o out.nv12
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

#define INPUT_SNIFF_BYTES 512

static void usage(void) {
    printf(
        "rkvc_encode — H.265 RKMPP 硬件编码工具\n"
        "\n"
        "用法:\n"
        "  rkvc_encode -o OUTPUT [选项]\n"
        "\n"
        "输入源 (三选一):\n"
        "  -i, --input FILE      输入原始 NV12 文件\n"
        "  --stdin               从 stdin 读取原始 NV12\n"
        "  --testsrc             使用测试图案\n"
        "\n"
        "输出:\n"
        "  -o, --output FILE     输出文件 (.h265/.mp4/.mkv)\n"
        "  --stdout              输出到 stdout (用于管道)\n"
        "\n"
        "参数:\n"
        "  -s, --size WxH        分辨率 (默认 1920x1080)\n"
        "  -r, --rate FPS        帧率 (默认 30)\n"
        "  -n, --frames N        帧数 (默认无限, 仅 --testsrc)\n"
        "  -b, --bitrate BPS     码率 (默认 4000000)\n"
        "  -p, --preset PRESET   编码预设: fast/medium/slow (默认 medium)\n"
        "  -v, --verbose         详细输出到 stderr\n"
        "  -h, --help            显示帮助\n"
        "\n"
        "示例:\n"
        "  # 离线编码 (输入必须是原始 NV12, 不是 .h265/.mp4 等压缩文件)\n"
        "  rkvc_encode -i raw.nv12 -o out.h265 -s 1920x1080\n"
        "\n"
        "  # 测试图案\n"
        "  rkvc_encode --testsrc -o out.h265 -n 300\n"
        "\n"
        "  # 管道: stdin → 编码 → stdout\n"
        "  cat raw.nv12 | rkvc_encode --stdin --stdout -s 1920x1080 > out.h265\n"
        "\n"
        "  # 管道: 编码 → 解码\n"
        "  rkvc_encode --stdin --stdout -s 1920x1080 < in.nv12 | \\\n"
        "    rkvc_decode --stdin -o decoded.nv12\n"
    );
}

static int input_file_is_compressed_video(FILE *fp)
{
    uint8_t header[INPUT_SNIFF_BYTES];
    long pos;
    size_t n;

    if (!fp)
        return 0;

    pos = ftell(fp);
    n = fread(header, 1, sizeof(header), fp);
    if (pos >= 0)
        fseek(fp, pos, SEEK_SET);
    else
        fseek(fp, 0, SEEK_SET);

    return rkvc_probe_input_format(header, n) == RKVC_INPUT_COMPRESSED_VIDEO;
}

static void print_compressed_input_error(const char *input)
{
    fprintf(stderr,
            "错误: rkvc_encode 需要原始 NV12 输入，但当前输入看起来是压缩视频码流或容器: %s\n"
            "提示: H.265/H.264/MP4/MKV 等文件请用 rkvc_decode 测试解码，或用 example_transcode / example_stream_transcode 测试转码。\n",
            input ? input : "stdin");
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

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1.0e9;
}

int main(int argc, char **argv) {
    const char *output = NULL;
    const char *input = NULL;
    int use_stdin = 0, use_stdout = 0, testsrc = 0;
    int width = 1920, height = 1080, fps = 30, frames = -1;
    int64_t bitrate = 4000000;
    rkvc_preset preset = RKVC_PRESET_MEDIUM;
    int verbose = 0;

    static struct option long_opts[] = {
        {"output",  required_argument, 0, 'o'},
        {"input",   required_argument, 0, 'i'},
        {"stdin",   no_argument,       0, 'I'},
        {"stdout",  no_argument,       0, 'O'},
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
        case 'I': use_stdin = 1; break;
        case 'O': use_stdout = 1; break;
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

    if (!output && !use_stdout) { usage(); return 1; }
    if (!input && !use_stdin && !testsrc) {
        fprintf(stderr, "错误: 需要 -i、--stdin 或 --testsrc\n");
        return 1;
    }

    FILE *in_fp = NULL;
    if (input) {
        in_fp = fopen(input, "rb");
        if (!in_fp) { perror("打开输入文件失败"); return 1; }
        if (input_file_is_compressed_video(in_fp)) {
            print_compressed_input_error(input);
            fclose(in_fp);
            return 1;
        }
    } else if (use_stdin) {
        in_fp = stdin;
    }

    rkvc_init();

    rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
    cfg.width = width; cfg.height = height;
    cfg.fps_num = fps; cfg.bitrate = bitrate;
    cfg.preset = preset;

    rkvc_encoder *enc = NULL;
    rkvc_err err;

    if (use_stdout) {
        /* stdout 模式: 用 muxer 输出到 stdout (hevc raw stream) */
        err = rkvc_encoder_open_file(&enc, &cfg, "pipe:1");
    } else {
        err = rkvc_encoder_open_file(&enc, &cfg, output);
    }

    if (err != RKVC_OK) {
        fprintf(stderr, "编码器打开失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    const char *src_name = testsrc ? "testsrc" : (use_stdin ? "stdin" : input);
    if (verbose)
        fprintf(stderr, "编码 %dx%d@%d, %s → %s\n", width, height, fps,
                src_name, use_stdout ? "stdout" : output);

    int frame_count = 0;
    size_t nv12_size = (size_t)width * height * 3 / 2;
    double t0 = now_sec();
    int failed = 0;

    for (int i = 0; testsrc ? (frames < 0 ? 1 : i < frames) : 1; i++) {
        rkvc_frame *f = NULL;
        rkvc_frame_alloc(&f, width, height, RKVC_PIX_FMT_NV12);

        if (testsrc) {
            generate_test_frame(f, i, width, height);
        } else {
            uint8_t *planes[4] = {0};
            int strides[4] = {0};
            rkvc_frame_get_data(f, planes, strides);

            int ok = 0;
            if (strides[0] == width) {
                ok = (fread(planes[0], 1, nv12_size, in_fp) == nv12_size);
            } else {
                ok = 1;
                for (int y = 0; y < height * 3 / 2; y++) {
                    if (fread(planes[0] + y * strides[0], 1, width, in_fp) != (size_t)width) {
                        ok = 0;
                        break;
                    }
                }
            }
            if (!ok) { rkvc_frame_unref(f); break; }
            rkvc_frame_set_pts(f, i);
        }

        err = rkvc_encoder_send_frame(enc, f);
        rkvc_frame_unref(f);

        if (err == RKVC_ERR_FORMAT) {
            print_compressed_input_error(src_name);
            failed = 1;
            break;
        }

        if (err != RKVC_OK && err != RKVC_ERR_AGAIN) {
            failed = 1;
            break;
        }

        /* 取出编码包 (muxer 自动写入) */
        rkvc_packet pkt;
        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {}

        frame_count++;
        if (verbose && frame_count % 100 == 0) {
            double elapsed = now_sec() - t0;
            fprintf(stderr, "  已编码 %d 帧, %.1f fps\n",
                    frame_count, elapsed > 0 ? frame_count / elapsed : 0);
        }
    }

    if (!failed) {
        /* flush */
        rkvc_encoder_drain(enc);
        rkvc_packet pkt;
        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {}
    }

    double elapsed = now_sec() - t0;
    rkvc_encoder_close(enc);

    if (in_fp && in_fp != stdin) fclose(in_fp);

    if (verbose)
        fprintf(stderr, "编码完成: %d 帧, %.3fs, %.1f fps → %s\n",
                frame_count, elapsed,
                elapsed > 0 ? frame_count / elapsed : 0,
                use_stdout ? "stdout" : output);

    rkvc_deinit();
    return failed ? 1 : 0;
}
