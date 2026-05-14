/**
 * @file latency_test.c
 * @brief 端到端延迟测试 — 模拟摄像头 → 编码 → 解码
 *
 * 模拟实时摄像头输入，生成带有运动纹理的 NV12 帧，
 * 经过 H.265 硬件编码 → 解码全链路，测量每帧端到端延迟。
 *
 * 用法:
 *   latency_test [-s 1920x1080] [-r 30] [-n 300] [-b 4000000] [-l]
 *
 *   -s WxH    分辨率 (默认 1920x1080)
 *   -r FPS    帧率 (默认 30)
 *   -n NUM    测试帧数 (默认 300)
 *   -b BPS    目标码率 (默认 4000000)
 *   -l        低延迟解码模式
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

/* ── 时间工具 ──────────────────────────────────────────────────────── */

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

/* ── 模拟摄像头帧生成器 ───────────────────────────────────────────── */

/**
 * 生成一帧模拟摄像头画面。
 *
 * Y 平面: 水平渐变 + 帧号滚动条 (模拟场景变化)
 * UV 平面: 固定色度
 */
static void generate_camera_frame(rkvc_frame *f, int frame_idx)
{
    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_get_data(f, planes, strides);

    rkvc_frame_info info;
    rkvc_frame_get_info(f, &info);
    int w = info.width;
    int h = info.height;

    /* Y 平面: 水平渐变 + 垂直滚动条 */
    if (planes[0]) {
        int bar_y = (frame_idx * 3) % h;  /* 滚动条位置 */
        int bar_h = 20;                    /* 滚动条高度 */
        for (int y = 0; y < h; y++) {
            uint8_t *row = planes[0] + y * strides[0];
            for (int x = 0; x < w; x++) {
                uint8_t base = (uint8_t)((x * 255) / w);
                /* 在滚动条区域反转亮度 */
                if (y >= bar_y && y < bar_y + bar_h)
                    base = 255 - base;
                row[x] = base;
            }
        }
    }

    /* UV 平面: 固定色度 128 (灰色) */
    if (planes[1]) {
        memset(planes[1], 128, strides[1] * (h / 2));
    }
}

/* ── 延迟统计 ──────────────────────────────────────────────────────── */

typedef struct {
    double *values;
    int     count;
    int     capacity;
} latency_stats;

static void latency_init(latency_stats *s, int capacity)
{
    s->values  = (double *)calloc(capacity, sizeof(double));
    s->count   = 0;
    s->capacity = capacity;
}

static void latency_add(latency_stats *s, double val)
{
    if (s->count < s->capacity)
        s->values[s->count++] = val;
}

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static double percentile(latency_stats *s, double pct)
{
    if (s->count == 0) return 0;
    qsort(s->values, s->count, sizeof(double), cmp_double);
    int idx = (int)(pct / 100.0 * (s->count - 1));
    if (idx < 0) idx = 0;
    if (idx >= s->count) idx = s->count - 1;
    return s->values[idx];
}

static double latency_avg(latency_stats *s)
{
    if (s->count == 0) return 0;
    double sum = 0;
    for (int i = 0; i < s->count; i++)
        sum += s->values[i];
    return sum / s->count;
}

static void latency_free(latency_stats *s)
{
    free(s->values);
    s->values = NULL;
}

/* ── 主流程 ─────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    int width = 1920, height = 1080, fps = 30, frames = 300;
    int bitrate = 4000000;
    int low_delay = 0;
    int c;

    static struct option long_opts[] = {
        {"size",    required_argument, 0, 's'},
        {"rate",    required_argument, 0, 'r'},
        {"frames",  required_argument, 0, 'n'},
        {"bitrate", required_argument, 0, 'b'},
        {"low-delay", no_argument,     0, 'l'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "s:r:n:b:lh", long_opts, NULL)) != -1) {
        switch (c) {
        case 's': sscanf(optarg, "%dx%d", &width, &height); break;
        case 'r': fps = atoi(optarg); break;
        case 'n': frames = atoi(optarg); break;
        case 'b': bitrate = atoi(optarg); break;
        case 'l': low_delay = 1; break;
        case 'h':
            printf("latency_test — 模拟摄像头端到端延迟测试\n"
                   "用法: latency_test [-s 1920x1080] [-r 30] [-n 300] [-b 4000000] [-l]\n");
            return 0;
        }
    }

    rkvc_init();

    printf("=== 端到端延迟测试 ===\n");
    printf("分辨率: %dx%d, 帧率: %d, 测试帧数: %d\n", width, height, fps, frames);
    printf("码率: %d bps, 低延迟: %s\n\n", bitrate, low_delay ? "是" : "否");

    /* ── 打开编码器 ──────────────────────────────────────────────── */

    rkvc_encoder *enc = NULL;
    rkvc_encoder_config ecfg = rkvc_encoder_config_defaults();
    ecfg.width    = width;
    ecfg.height   = height;
    ecfg.fps_num  = fps;
    ecfg.bitrate  = bitrate;

    rkvc_err err = rkvc_encoder_open(&enc, &ecfg);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开编码器失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    /* ── 打开解码器 ──────────────────────────────────────────────── */

    rkvc_decoder *dec = NULL;
    rkvc_decoder_config dcfg = rkvc_decoder_config_defaults();
    dcfg.low_delay = low_delay;

    err = rkvc_decoder_open(&dec, &dcfg);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开解码器失败: %s\n", rkvc_err_str(err));
        rkvc_encoder_close(enc);
        return 1;
    }

    /* ── 延迟统计 ────────────────────────────────────────────────── */

    latency_stats lat_enc;    /* 采集→编码完成 */
    latency_stats lat_full;   /* 采集→解码完成 (端到端) */
    latency_init(&lat_enc,  frames);
    latency_init(&lat_full, frames);

    double frame_interval_ms = 1000.0 / fps;
    double t_start = now_ms();
    int encoded = 0, decoded = 0;
    uint64_t total_bytes = 0;
    int next_out = 0;  /* 下一个待输出的源帧序号 */

    /* 记录每帧采集时间戳，用于解码后计算端到端延迟 */
    double *capture_ts = (double *)calloc(frames, sizeof(double));

    printf("帧号  采集时间  编码延迟  解码延迟  端到端延迟  编码大小\n");
    printf("─────  ────────  ────────  ────────  ─────────  ────────\n");

    /* ── 主循环: 模拟摄像头实时采集 ──────────────────────────────── */

    for (int i = 0; i < frames; i++) {
        /* 1. 模拟摄像头采集时间 */
        double t_capture = now_ms();
        capture_ts[i] = t_capture;

        /* 2. 生成帧 */
        rkvc_frame *f = NULL;
        rkvc_frame_alloc(&f, width, height, RKVC_PIX_FMT_NV12);
        generate_camera_frame(f, i);
        rkvc_frame_set_pts(f, (int64_t)i);

        /* 3. 编码 */
        err = rkvc_encoder_send_frame(enc, f);
        if (err != RKVC_OK) {
            fprintf(stderr, "编码帧 %d 失败: %s\n", i, rkvc_err_str(err));
            rkvc_frame_unref(f);
            continue;
        }
        rkvc_frame_unref(f);

        /* 4. 取编码包 */
        rkvc_packet pkt;
        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
            double t_encoded = now_ms();
            double enc_latency = t_encoded - t_capture;
            latency_add(&lat_enc, enc_latency);
            total_bytes += pkt.size;
            encoded++;

            /* 记录编码包对应的源帧号 */
            int pkt_src = (int)pkt.pts;

            /* 5. 送入解码器 */
            err = rkvc_decoder_send_packet(dec, pkt.data, pkt.size,
                                           pkt.pts, pkt.dts);
            if (err != RKVC_OK) {
                fprintf(stderr, "送入解码器失败: %s\n", rkvc_err_str(err));
                continue;
            }

            /* 6. 取解码帧
             * RKMPP 硬件解码器不保留 PTS，用顺序计数器映射。
             * 低延迟 + 无 B 帧时，解码器按顺序输出。
             */
            rkvc_frame *out_frame = NULL;
            while (rkvc_decoder_receive_frame(dec, &out_frame) == RKVC_OK) {
                double t_decoded = now_ms();
                int src_frame = next_out++;
                decoded++;

                if (src_frame >= 0 && src_frame < frames) {
                    double e2e = t_decoded - capture_ts[src_frame];
                    latency_add(&lat_full, e2e);

                    if (decoded <= 10 || decoded % 30 == 0) {
                        printf(" %3d   %7.1f   %6.1f   %6.1f    %7.1f    %6d B\n",
                               src_frame,
                               capture_ts[src_frame] - t_start,
                               enc_latency,
                               t_decoded - t_encoded,
                               e2e,
                               pkt.size);
                    }
                }
                rkvc_frame_unref(out_frame);
            }
        }

        /* 模拟实时: 等到下一帧采集时间 */
        double target_ms = t_start + (i + 1) * frame_interval_ms;
        double sleep_ms = target_ms - now_ms();
        if (sleep_ms > 0)
            usleep((unsigned int)(sleep_ms * 1000));
    }

    /* ── Flush 编码器 ────────────────────────────────────────────── */

    rkvc_encoder_drain(enc);

    rkvc_packet pkt;
    while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
        encoded++;
        total_bytes += pkt.size;

        rkvc_decoder_send_packet(dec, pkt.data, pkt.size, pkt.pts, pkt.dts);

        rkvc_frame *out_frame = NULL;
        while (rkvc_decoder_receive_frame(dec, &out_frame) == RKVC_OK) {
            decoded++;
            rkvc_frame_unref(out_frame);
        }
    }

    /* Flush 解码器 */
    rkvc_decoder_drain(dec);
    rkvc_frame *out_frame = NULL;
    while (rkvc_decoder_receive_frame(dec, &out_frame) == RKVC_OK) {
        decoded++;
        rkvc_frame_unref(out_frame);
    }

    double total_time = now_ms() - t_start;

    /* ── 报告 ────────────────────────────────────────────────────── */

    printf("\n=== 测试结果 ===\n");
    printf("总帧数:     %d (编码 %d, 解码 %d)\n", frames, encoded, decoded);
    printf("总时间:     %.3f s\n", total_time / 1000.0);
    printf("实际帧率:   %.1f fps\n", encoded / (total_time / 1000.0));
    printf("总编码数据: %.2f KB\n", total_bytes / 1024.0);
    printf("平均码率:   %.2f kbps\n", total_bytes * 8.0 / (total_time / 1000.0) / 1000.0);

    if (lat_enc.count > 0) {
        printf("\n--- 采集→编码完成 延迟 ---\n");
        printf("  平均: %.2f ms\n", latency_avg(&lat_enc));
        printf("  最小: %.2f ms\n", percentile(&lat_enc, 0));
        printf("  P50:  %.2f ms\n", percentile(&lat_enc, 50));
        printf("  P95:  %.2f ms\n", percentile(&lat_enc, 95));
        printf("  P99:  %.2f ms\n", percentile(&lat_enc, 99));
        printf("  最大: %.2f ms\n", percentile(&lat_enc, 100));
    }

    if (lat_full.count > 0) {
        printf("\n--- 端到端延迟 (采集→解码完成) ---\n");
        printf("  平均: %.2f ms\n", latency_avg(&lat_full));
        printf("  最小: %.2f ms\n", percentile(&lat_full, 0));
        printf("  P50:  %.2f ms\n", percentile(&lat_full, 50));
        printf("  P95:  %.2f ms\n", percentile(&lat_full, 95));
        printf("  P99:  %.2f ms\n", percentile(&lat_full, 99));
        printf("  最大: %.2f ms\n", percentile(&lat_full, 100));
    }

    /* ── 清理 ────────────────────────────────────────────────────── */

    rkvc_encoder_close(enc);
    rkvc_decoder_close(dec);
    latency_free(&lat_enc);
    latency_free(&lat_full);
    free(capture_ts);
    rkvc_deinit();

    return 0;
}
