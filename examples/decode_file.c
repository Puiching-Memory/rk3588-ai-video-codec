/**
 * @file decode_file.c
 * @brief 示例: 离线 H.265 文件解码，支持指定输出像素格式。
 *
 * 将 H.265 文件解码为原始帧，可选写入文件，统计信息。
 * 通过 --format 可指定 NV12 / YUV420P / NV16 / P010 输出格式。
 *
 * 用法:
 *   decode_file input.h265 [-o output.nv12] [--format NV12]
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

static rkvc_pix_fmt parse_format(const char *s)
{
    if (!s)
        return RKVC_PIX_FMT_NV12;
    if (!strcmp(s, "nv12") || !strcmp(s, "NV12"))
        return RKVC_PIX_FMT_NV12;
    if (!strcmp(s, "yuv420p") || !strcmp(s, "YUV420P"))
        return RKVC_PIX_FMT_YUV420P;
    if (!strcmp(s, "nv16") || !strcmp(s, "NV16"))
        return RKVC_PIX_FMT_NV16;
    if (!strcmp(s, "p010") || !strcmp(s, "P010"))
        return RKVC_PIX_FMT_P010;
    fprintf(stderr, "未知格式: %s (支持: NV12 YUV420P NV16 P010)\n", s);
    return (rkvc_pix_fmt)-1;
}

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

/* 根据格式返回每帧总字节数的近似值 */
static size_t frame_bytes(rkvc_pix_fmt fmt, int w, int h)
{
    switch (fmt) {
    case RKVC_PIX_FMT_NV12:
    case RKVC_PIX_FMT_YUV420P:
    case RKVC_PIX_FMT_P010:
        return (size_t)w * h * 3 / 2 * (fmt == RKVC_PIX_FMT_P010 ? 2 : 1);
    case RKVC_PIX_FMT_NV16:
        return (size_t)w * h * 2;
    default:
        return (size_t)w * h * 3 / 2;
    }
}

int main(int argc, char **argv)
{
    const char *input  = NULL;
    const char *output = NULL;
    const char *fmt_str = NULL;
    int verbose = 0;
    int c;

    static struct option long_opts[] = {
        {"output",  required_argument, 0, 'o'},
        {"format",  required_argument, 0, 'f'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "o:f:vh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'o': output = optarg; break;
        case 'f': fmt_str = optarg; break;
        case 'v': verbose = 1; break;
        case 'h':
            printf("decode_file — 离线 H.265 解码示例\n"
                   "  输入文件作为位置参数\n"
                   "  -o FILE        输出原始帧文件 (可选)\n"
                   "  -f, --format F 输出格式: NV12 YUV420P NV16 P010 (默认 NV12)\n"
                   "  -v             详细输出\n");
            return 0;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "用法: decode_file <input.h265> [-o output] [--format NV12]\n");
        return 1;
    }
    input = argv[optind];

    rkvc_pix_fmt out_fmt = parse_format(fmt_str);
    if ((int)out_fmt < 0)
        return 1;

    rkvc_init();

    rkvc_decoder *dec = NULL;
    rkvc_decoder_config cfg = rkvc_decoder_config_defaults();
    cfg.output_format = out_fmt;

    rkvc_err err = rkvc_decoder_open_file(&dec, &cfg, input);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开解码器失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    int width = 0, height = 0, fps_num = 30, fps_den = 1;
    rkvc_decoder_get_video_info(dec, &width, &height, &fps_num, &fps_den);
    printf("解码 %s: %dx%d @ %d/%d fps  输出格式 %s\n",
           input, width, height, fps_num, fps_den, fmt_name(out_fmt));

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
    int format_mismatch = 0;

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
            rkvc_frame_info info;
            rkvc_frame_get_info(f, &info);

            if (info.format != out_fmt) {
                if (!format_mismatch)
                    fprintf(stderr, "警告: 帧 %d 格式=%s (期望 %s)\n",
                            frame_count, fmt_name(info.format),
                            fmt_name(out_fmt));
                format_mismatch = 1;
            }

            if (out_fp) {
                uint8_t *planes[4] = {0};
                int strides[4] = {0};
                rkvc_frame_get_data(f, planes, strides);

                int n_planes = 1;
                int plane_h[4] = {height, 0, 0, 0};
                int plane_w[4] = {width, 0, 0, 0};

                switch (out_fmt) {
                case RKVC_PIX_FMT_NV12:
                case RKVC_PIX_FMT_YUV420P:
                    n_planes = 2;
                    plane_h[1] = height / 2;
                    plane_w[1] = width;
                    break;
                case RKVC_PIX_FMT_NV16:
                    n_planes = 2;
                    plane_h[1] = height;
                    plane_w[1] = width;
                    break;
                case RKVC_PIX_FMT_P010:
                    n_planes = 2;
                    plane_h[1] = height / 2;
                    plane_w[1] = width;
                    break;
                default:
                    break;
                }

                for (int p = 0; p < n_planes; p++) {
                    int bytes_per_pixel = (out_fmt == RKVC_PIX_FMT_P010) ? 2 : 1;
                    for (int y = 0; y < plane_h[p]; y++)
                        fwrite(planes[p] + y * strides[p], 1,
                               plane_w[p] * bytes_per_pixel, out_fp);
                }
            }

            total_bytes += frame_bytes(out_fmt, width, height);
            frame_count++;

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
    if (format_mismatch)
        printf("格式验证: 失败 (存在格式不匹配)\n");
    else
        printf("格式验证: 全部匹配 %s ✓\n", fmt_name(out_fmt));

    rkvc_decoder_close(dec);
    rkvc_deinit();
    return format_mismatch ? 1 : 0;
}
