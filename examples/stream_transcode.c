/**
 * @file stream_transcode.c
 * @brief 示例: 流式转码管线 (解码 → 流式编码)。
 *
 * 使用文件解码器读取 H.265 输入，通过流式编码 API 实时重新编码。
 * 当输入输出分辨率不同时，流式 API 内部自动调用 RGA 硬件缩放。
 *
 * 用法:
 *   stream_transcode -i input.h265 -o output.h265 [-s WxH] [-b BPS]
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
    const char *output = "stream_transcode_out.h265";
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
            printf("stream_transcode — 流式转码示例\n"
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

    /* 打开文件解码器 */
    rkvc_decoder *dec = NULL;
    rkvc_decoder_config dcfg = rkvc_decoder_config_defaults();

    rkvc_err err = rkvc_decoder_open_file(&dec, &dcfg, input);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开解码器失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    int src_w = 0, src_h = 0, fps_num = 30, fps_den = 1;
    rkvc_decoder_get_video_info(dec, &src_w, &src_h, &fps_num, &fps_den);

    if (out_width <= 0)  out_width  = src_w;
    if (out_height <= 0) out_height = src_h;

    int need_scale = (out_width != src_w) || (out_height != src_h);

    printf("流式转码: %s (%dx%d) → %s (%dx%d @ %d kbps)%s\n",
           input, src_w, src_h, output, out_width, out_height,
           out_bitrate / 1000,
           need_scale ? " [自动 RGA 缩放]" : "");

    /* 创建流式编码器 (尺寸不匹配时自动缩放) */
    rkvc_stream_config scfg = rkvc_stream_config_defaults();
    scfg.direction = RKVC_STREAM_ENCODE;
    scfg.width     = out_width;
    scfg.height    = out_height;
    scfg.fps_num   = fps_num;
    scfg.fps_den   = fps_den;
    scfg.bitrate   = out_bitrate;

    rkvc_stream *enc_stream = NULL;
    err = rkvc_stream_open(&enc_stream, &scfg);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开流失败: %s\n", rkvc_err_str(err));
        rkvc_decoder_close(dec);
        return 1;
    }

    double t0 = now_sec();
    int frame_count = 0;
    int encoded = 0;
    uint64_t total_bytes = 0;

    /* 打开输出文件 */
    FILE *fout = fopen(output, "wb");
    if (!fout) {
        fprintf(stderr, "打开输出文件失败: %s\n", output);
        rkvc_stream_close(enc_stream);
        rkvc_decoder_close(dec);
        return 1;
    }

    /* 解码 → 流式编码管线 (缩放由 stream API 内部处理) */
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
            err = rkvc_stream_push(enc_stream, f);
            rkvc_frame_unref(f);

            if (err != RKVC_OK && err != RKVC_ERR_AGAIN) {
                fprintf(stderr, "编码失败: %s\n", rkvc_err_str(err));
                break;
            }

            rkvc_packet pkt;
            while (rkvc_stream_pull(enc_stream, &pkt, 0) == RKVC_OK) {
                fwrite(pkt.data, 1, pkt.size, fout);
                encoded++;
                total_bytes += pkt.size;
            }

            frame_count++;
            if (frame_count % 100 == 0) {
                rkvc_stream_stats stats;
                rkvc_stream_get_stats(enc_stream, &stats);
                printf("  已转码 %d 帧, 编码 %d 包\n",
                       frame_count, encoded);
            }
        }
    }

    /* flush 编码器 */
    rkvc_stream_finish(enc_stream);

    rkvc_packet pkt;
    while (rkvc_stream_pull(enc_stream, &pkt, -1) == RKVC_OK) {
        fwrite(pkt.data, 1, pkt.size, fout);
        encoded++;
        total_bytes += pkt.size;
    }

    fclose(fout);

    double elapsed = now_sec() - t0;

    rkvc_stream_stats stats;
    rkvc_stream_get_stats(enc_stream, &stats);

    printf("\n流式转码完成:\n");
    printf("  解码帧数: %d\n", frame_count);
    printf("  编码包数: %d\n", encoded);
    printf("  总时间:   %.3fs\n", elapsed);
    printf("  平均帧率: %.1f fps\n",
           elapsed > 0 ? frame_count / elapsed : 0);
    printf("  总数据量: %.2f KB\n", total_bytes / 1024.0);

    rkvc_stream_close(enc_stream);
    rkvc_decoder_close(dec);
    rkvc_deinit();
    return 0;
}
