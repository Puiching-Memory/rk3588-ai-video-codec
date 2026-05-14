/**
 * @file psnr_test.c
 * @brief 示例: 端到端 PSNR 测试 (解码 → 编码 → 解码 → 比较)。
 *
 * 将输入 H.265 文件解码为原始帧，重新编码，再解码，
 * 逐帧计算 PSNR (Y/U/V 分量及加权平均)。
 *
 * 用法:
 *   psnr_test -i input.h265 [-b 4000000] [-n MAX_FRAMES]
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <time.h>

/* ── 存储一帧 NV12 数据 ─────────────────────────────────────────── */

typedef struct {
    uint8_t *y;
    uint8_t *uv;
    int      width;
    int      height;
    int      y_stride;
    int      uv_stride;
} stored_frame;

static void stored_frame_free(stored_frame *sf)
{
    if (sf) {
        free(sf->y);
        free(sf->uv);
    }
}

static int stored_frame_copy(stored_frame *dst, const rkvc_frame *src)
{
    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_get_data(src, planes, strides);

    rkvc_frame_info info;
    rkvc_frame_get_info(src, &info);

    dst->width     = info.width;
    dst->height    = info.height;
    dst->y_stride  = strides[0];
    dst->uv_stride = strides[1];

    dst->y  = malloc((size_t)strides[0] * info.height);
    dst->uv = malloc((size_t)strides[1] * (info.height / 2));
    if (!dst->y || !dst->uv) {
        stored_frame_free(dst);
        return -1;
    }

    for (int row = 0; row < info.height; row++)
        memcpy(dst->y + row * strides[0],
               planes[0] + row * strides[0], info.width);

    for (int row = 0; row < info.height / 2; row++)
        memcpy(dst->uv + row * strides[1],
               planes[1] + row * strides[1], info.width);

    return 0;
}

/* ── PSNR 计算 ─────────────────────────────────────────────────── */

static double calc_plane_psnr(const uint8_t *orig, int orig_stride,
                              const uint8_t *recon, int recon_stride,
                              int w, int h)
{
    double sse = 0.0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double d = (double)orig[y * orig_stride + x]
                     - (double)recon[y * recon_stride + x];
            sse += d * d;
        }
    }

    double mse = sse / ((double)w * h);
    if (mse < 1e-10)
        return 99.99; /* 完全一致 */
    return 10.0 * log10(255.0 * 255.0 / mse);
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1.0e9;
}

/* ── 主函数 ─────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    const char *input    = NULL;
    int out_bitrate      = 4000000;
    int max_frames       = 0;   /* 0 = 不限制 */
    int verbose          = 0;
    int c;

    static struct option long_opts[] = {
        {"input",   required_argument, 0, 'i'},
        {"bitrate", required_argument, 0, 'b'},
        {"frames",  required_argument, 0, 'n'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "i:b:n:vh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'i': input = optarg; break;
        case 'b': out_bitrate = atoi(optarg); break;
        case 'n': max_frames = atoi(optarg); break;
        case 'v': verbose = 1; break;
        case 'h':
            printf("psnr_test — 端到端 PSNR 测试\n"
                   "  -i FILE    输入 H.265 文件\n"
                   "  -b BPS     编码码率 (默认 4M)\n"
                   "  -n N       最大测试帧数 (默认全部)\n"
                   "  -v         逐帧 PSNR 输出\n");
            return 0;
        }
    }

    if (!input) {
        fprintf(stderr, "需要指定输入文件: -i <file>\n");
        return 1;
    }

    rkvc_init();

    /* ================================================================
     * 阶段 1: 解码输入文件，保存原始帧，同时送入编码器
     * ================================================================ */
    double t_start = now_sec();

    rkvc_decoder *dec_in = NULL;
    rkvc_decoder_config dcfg = rkvc_decoder_config_defaults();
    rkvc_err err = rkvc_decoder_open_file(&dec_in, &dcfg, input);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开输入解码器失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    int width = 0, height = 0, fps_num = 30, fps_den = 1;
    rkvc_decoder_get_video_info(dec_in, &width, &height, &fps_num, &fps_den);
    printf("输入: %s (%dx%d @ %d/%d fps)\n",
           input, width, height, fps_num, fps_den);

    /* 打开编码器 (内存模式，不写文件) */
    rkvc_encoder *enc = NULL;
    rkvc_encoder_config ecfg = rkvc_encoder_config_defaults();
    ecfg.width   = width;
    ecfg.height  = height;
    ecfg.fps_num = fps_num;
    ecfg.fps_den = fps_den;
    ecfg.bitrate = out_bitrate;

    err = rkvc_encoder_open(&enc, &ecfg);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开编码器失败: %s\n", rkvc_err_str(err));
        rkvc_decoder_close(dec_in);
        return 1;
    }

    /* 存储原始帧 */
    stored_frame *orig_frames = NULL;
    int orig_cap = 0;
    int orig_count = 0;

    /* 收集编码后的包 */
    uint8_t **packets = NULL;
    int *pkt_sizes    = NULL;
    int64_t *pkt_pts  = NULL;
    int64_t *pkt_dts  = NULL;
    int pkt_cap = 0;
    int pkt_count = 0;

    printf("正在解码 + 编码...\n");

    for (;;) {
        if (max_frames > 0 && orig_count >= max_frames)
            break;

        err = rkvc_decoder_read_packet(dec_in);
        if (err == RKVC_ERR_EOF)
            break;
        if (err != RKVC_OK) {
            fprintf(stderr, "读取包失败: %s\n", rkvc_err_str(err));
            break;
        }

        rkvc_frame *f = NULL;
        while (rkvc_decoder_receive_frame(dec_in, &f) == RKVC_OK) {
            if (max_frames > 0 && orig_count >= max_frames) {
                rkvc_frame_unref(f);
                break;
            }

            /* 扩展数组 */
            if (orig_count >= orig_cap) {
                orig_cap = orig_cap ? orig_cap * 2 : 256;
                orig_frames = realloc(orig_frames,
                                      sizeof(stored_frame) * orig_cap);
            }

            /* 保存原始帧 */
            if (stored_frame_copy(&orig_frames[orig_count], f) != 0) {
                fprintf(stderr, "内存分配失败\n");
                rkvc_frame_unref(f);
                goto cleanup;
            }
            orig_count++;

            /* 送入编码器 */
            err = rkvc_encoder_send_frame(enc, f);
            rkvc_frame_unref(f);

            if (err != RKVC_OK && err != RKVC_ERR_AGAIN) {
                fprintf(stderr, "编码失败: %s\n", rkvc_err_str(err));
                goto cleanup;
            }

            /* 收集编码包 */
            rkvc_packet pkt;
            while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
                if (pkt_count >= pkt_cap) {
                    pkt_cap   = pkt_cap ? pkt_cap * 2 : 256;
                    packets   = realloc(packets, sizeof(uint8_t *) * pkt_cap);
                    pkt_sizes = realloc(pkt_sizes, sizeof(int) * pkt_cap);
                    pkt_pts   = realloc(pkt_pts, sizeof(int64_t) * pkt_cap);
                    pkt_dts   = realloc(pkt_dts, sizeof(int64_t) * pkt_cap);
                }
                packets[pkt_count] = malloc(pkt.size);
                memcpy(packets[pkt_count], pkt.data, pkt.size);
                pkt_sizes[pkt_count] = pkt.size;
                pkt_pts[pkt_count]   = pkt.pts;
                pkt_dts[pkt_count]   = pkt.dts;
                pkt_count++;
            }

            if (verbose && orig_count % 100 == 0)
                printf("  已处理 %d 帧\n", orig_count);
        }
    }

    /* flush 编码器 */
    rkvc_encoder_send_frame(enc, NULL);
    {
        rkvc_packet pkt;
        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
            if (pkt_count >= pkt_cap) {
                pkt_cap   = pkt_cap ? pkt_cap * 2 : 256;
                packets   = realloc(packets, sizeof(uint8_t *) * pkt_cap);
                pkt_sizes = realloc(pkt_sizes, sizeof(int) * pkt_cap);
                pkt_pts   = realloc(pkt_pts, sizeof(int64_t) * pkt_cap);
                pkt_dts   = realloc(pkt_dts, sizeof(int64_t) * pkt_cap);
            }
            packets[pkt_count] = malloc(pkt.size);
            memcpy(packets[pkt_count], pkt.data, pkt.size);
            pkt_sizes[pkt_count] = pkt.size;
            pkt_pts[pkt_count]   = pkt.pts;
            pkt_dts[pkt_count]   = pkt.dts;
            pkt_count++;
        }
    }

    rkvc_encoder_close(enc);
    rkvc_decoder_close(dec_in);

    printf("阶段 1 完成: %d 帧, %d 个编码包\n", orig_count, pkt_count);

    /* ================================================================
     * 阶段 2: 解码编码后的包，与原始帧逐帧比较
     * ================================================================ */
    printf("正在解码编码数据并计算 PSNR...\n");

    rkvc_decoder *dec_out = NULL;
    rkvc_decoder_config dcfg2 = rkvc_decoder_config_defaults();
    err = rkvc_decoder_open(&dec_out, &dcfg2);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开重放解码器失败: %s\n", rkvc_err_str(err));
        goto cleanup;
    }

    double sum_psnr_y  = 0.0;
    double sum_psnr_u  = 0.0;
    double sum_psnr_v  = 0.0;
    double sum_psnr_avg = 0.0;
    double min_psnr_y  = 999.0;
    int compared = 0;

    int recon_idx = 0; /* 已解码的重建帧索引 */

    for (int i = 0; i < pkt_count; i++) {
        err = rkvc_decoder_send_packet(dec_out, packets[i], pkt_sizes[i],
                                       pkt_pts[i], pkt_dts[i]);
        if (err != RKVC_OK) {
            fprintf(stderr, "送入重建解码器失败: %s\n", rkvc_err_str(err));
            break;
        }

        rkvc_frame *recon = NULL;
        while (rkvc_decoder_receive_frame(dec_out, &recon) == RKVC_OK) {
            if (recon_idx >= orig_count) {
                rkvc_frame_unref(recon);
                break;
            }

            uint8_t *r_planes[4] = {0};
            int r_strides[4] = {0};
            rkvc_frame_get_data(recon, r_planes, r_strides);

            stored_frame *orig = &orig_frames[recon_idx];

            /* Y 平面 PSNR */
            double psnr_y = calc_plane_psnr(
                orig->y, orig->y_stride,
                r_planes[0], r_strides[0],
                width, height);

            /* U 平面 PSNR (NV12: UV 交错, 取偶数位置为 U) */
            double psnr_u = 0.0, psnr_v = 0.0;
            {
                double sse_u = 0.0, sse_v = 0.0;
                int uv_h = height / 2;
                int uv_w = width / 2;
                for (int y = 0; y < uv_h; y++) {
                    for (int x = 0; x < uv_w; x++) {
                        uint8_t o_u = orig->uv[y * orig->uv_stride + x * 2];
                        uint8_t o_v = orig->uv[y * orig->uv_stride + x * 2 + 1];
                        uint8_t r_u = r_planes[1][y * r_strides[1] + x * 2];
                        uint8_t r_v = r_planes[1][y * r_strides[1] + x * 2 + 1];
                        double du = (double)o_u - (double)r_u;
                        double dv = (double)o_v - (double)r_v;
                        sse_u += du * du;
                        sse_v += dv * dv;
                    }
                }
                double mse_u = sse_u / ((double)uv_w * uv_h);
                double mse_v = sse_v / ((double)uv_w * uv_h);
                psnr_u = (mse_u < 1e-10) ? 99.99
                         : 10.0 * log10(255.0 * 255.0 / mse_u);
                psnr_v = (mse_v < 1e-10) ? 99.99
                         : 10.0 * log10(255.0 * 255.0 / mse_v);
            }

            /* 加权平均: Y 占 6/9, U 占 1.5/9, V 占 1.5/9 */
            double psnr_weighted = (6.0 * psnr_y + 1.5 * psnr_u + 1.5 * psnr_v)
                                   / 9.0;

            sum_psnr_y  += psnr_y;
            sum_psnr_u  += psnr_u;
            sum_psnr_v  += psnr_v;
            sum_psnr_avg += psnr_weighted;
            if (psnr_y < min_psnr_y)
                min_psnr_y = psnr_y;
            compared++;

            if (verbose) {
                printf("  帧 %4d: Y=%.2f dB  U=%.2f dB  V=%.2f dB  "
                       "加权=%.2f dB\n",
                       recon_idx, psnr_y, psnr_u, psnr_v, psnr_weighted);
            }

            rkvc_frame_unref(recon);
            recon_idx++;
        }
    }

    /* flush 重建解码器 */
    {
        rkvc_frame *recon = NULL;
        while (rkvc_decoder_receive_frame(dec_out, &recon) == RKVC_OK) {
            if (recon_idx >= orig_count) {
                rkvc_frame_unref(recon);
                break;
            }

            uint8_t *r_planes[4] = {0};
            int r_strides[4] = {0};
            rkvc_frame_get_data(recon, r_planes, r_strides);

            stored_frame *orig = &orig_frames[recon_idx];

            double psnr_y = calc_plane_psnr(
                orig->y, orig->y_stride,
                r_planes[0], r_strides[0],
                width, height);

            double sse_u = 0.0, sse_v = 0.0;
            int uv_h = height / 2;
            int uv_w = width / 2;
            for (int y = 0; y < uv_h; y++) {
                for (int x = 0; x < uv_w; x++) {
                    uint8_t o_u = orig->uv[y * orig->uv_stride + x * 2];
                    uint8_t o_v = orig->uv[y * orig->uv_stride + x * 2 + 1];
                    uint8_t r_u = r_planes[1][y * r_strides[1] + x * 2];
                    uint8_t r_v = r_planes[1][y * r_strides[1] + x * 2 + 1];
                    double du = (double)o_u - (double)r_u;
                    double dv = (double)o_v - (double)r_v;
                    sse_u += du * du;
                    sse_v += dv * dv;
                }
            }
            double mse_u = sse_u / ((double)uv_w * uv_h);
            double mse_v = sse_v / ((double)uv_w * uv_h);
            double psnr_u = (mse_u < 1e-10) ? 99.99
                            : 10.0 * log10(255.0 * 255.0 / mse_u);
            double psnr_v = (mse_v < 1e-10) ? 99.99
                            : 10.0 * log10(255.0 * 255.0 / mse_v);
            double psnr_weighted = (6.0 * psnr_y + 1.5 * psnr_u + 1.5 * psnr_v)
                                   / 9.0;

            sum_psnr_y  += psnr_y;
            sum_psnr_u  += psnr_u;
            sum_psnr_v  += psnr_v;
            sum_psnr_avg += psnr_weighted;
            if (psnr_y < min_psnr_y)
                min_psnr_y = psnr_y;
            compared++;

            if (verbose) {
                printf("  帧 %4d: Y=%.2f dB  U=%.2f dB  V=%.2f dB  "
                       "加权=%.2f dB\n",
                       recon_idx, psnr_y, psnr_u, psnr_v, psnr_weighted);
            }

            rkvc_frame_unref(recon);
            recon_idx++;
        }
    }

    rkvc_decoder_close(dec_out);

    double elapsed = now_sec() - t_start;

    /* ── 输出结果 ──────────────────────────────────────────────── */
    printf("\n");
    printf("══════════════════════════════════════════\n");
    printf("  端到端 PSNR 测试结果\n");
    printf("══════════════════════════════════════════\n");
    printf("  输入:        %s\n", input);
    printf("  分辨率:      %dx%d\n", width, height);
    printf("  码率:        %d kbps\n", out_bitrate / 1000);
    printf("  比较帧数:    %d\n", compared);
    printf("──────────────────────────────────────────\n");
    if (compared > 0) {
        printf("  Y 平均:      %.2f dB\n", sum_psnr_y / compared);
        printf("  U 平均:      %.2f dB\n", sum_psnr_u / compared);
        printf("  V 平均:      %.2f dB\n", sum_psnr_v / compared);
        printf("  加权平均:    %.2f dB\n", sum_psnr_avg / compared);
        printf("  Y 最低:      %.2f dB\n", min_psnr_y);
    }
    printf("  耗时:        %.3f s\n", elapsed);
    printf("══════════════════════════════════════════\n");

cleanup:
    for (int i = 0; i < orig_count; i++)
        stored_frame_free(&orig_frames[i]);
    free(orig_frames);

    for (int i = 0; i < pkt_count; i++)
        free(packets[i]);
    free(packets);
    free(pkt_sizes);
    free(pkt_pts);
    free(pkt_dts);

    return (compared > 0) ? 0 : 1;
}
