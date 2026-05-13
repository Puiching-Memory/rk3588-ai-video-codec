/**
 * @file rkvc_bench.c
 * @brief RK3588 VPU 基准测试工具。
 *
 * 对比 RKMPP 硬件编解码和软件编解码的吞吐量。
 *
 * 用法:
 *   rkvc_bench [选项]
 *
 * 选项:
 *   -s, --size WxH     分辨率 (默认 1920x1080)
 *   -r, --rate FPS     帧率 (默认 30)
 *   -n, --frames N     帧数 (默认 300)
 *   -b, --bitrate BPS  码率 bps (默认 4000000)
 *   --4k               测试 4K (3840x2160)
 *   --quick            快速模式 (120 帧)
 *   --encode-only      仅测试编码
 *   --decode-only      仅测试解码
 *   --stream           测试流式 API
 *   -o, --output DIR   结果输出目录
 *   -v, --verbose      详细输出
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

/* ── 命令行参数 ────────────────────────────────────────────────────── */

typedef struct {
    int  width;
    int  height;
    int  fps;
    int  frames;
    int  bitrate;
    int  encode_only;
    int  decode_only;
    int  test_stream;
    int  verbose;
    char output_dir[256];
} bench_opts;

static bench_opts parse_args(int argc, char **argv)
{
    bench_opts opts = {
        .width = 1920, .height = 1080, .fps = 30,
        .frames = 300, .bitrate = 4000000,
        .encode_only = 0, .decode_only = 0,
        .test_stream = 0, .verbose = 0,
        .output_dir = "bench_results",
    };

    static struct option long_opts[] = {
        {"size",      required_argument, 0, 's'},
        {"rate",      required_argument, 0, 'r'},
        {"frames",    required_argument, 0, 'n'},
        {"bitrate",   required_argument, 0, 'b'},
        {"4k",        no_argument,       0, '4'},
        {"quick",     no_argument,       0, 'q'},
        {"encode-only", no_argument,     0, 'E'},
        {"decode-only", no_argument,     0, 'D'},
        {"stream",    no_argument,       0, 'S'},
        {"output",    required_argument, 0, 'o'},
        {"verbose",   no_argument,       0, 'v'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "s:r:n:b:qvho:", long_opts, NULL)) != -1) {
        switch (c) {
        case 's':
            sscanf(optarg, "%dx%d", &opts.width, &opts.height);
            break;
        case 'r': opts.fps = atoi(optarg); break;
        case 'n': opts.frames = atoi(optarg); break;
        case 'b': opts.bitrate = atoi(optarg); break;
        case '4':
            opts.width = 3840; opts.height = 2160;
            break;
        case 'q': opts.frames = 120; break;
        case 'E': opts.encode_only = 1; break;
        case 'D': opts.decode_only = 1; break;
        case 'S': opts.test_stream = 1; break;
        case 'o':
            strncpy(opts.output_dir, optarg, sizeof(opts.output_dir) - 1);
            break;
        case 'v': opts.verbose = 1; break;
        case 'h':
            printf("rkvc_bench — RK3588 VPU 基准测试\n"
                   "用法: rkvc_bench [选项]\n"
                   "  -s WxH      分辨率 (默认 1920x1080)\n"
                   "  -r FPS      帧率 (默认 30)\n"
                   "  -n N        帧数 (默认 300)\n"
                   "  -b BPS      码率 (默认 4M)\n"
                   "  --4k        测试 4K\n"
                   "  --quick     快速模式 (120 帧)\n"
                   "  --encode-only  仅编码\n"
                   "  --decode-only  仅解码\n"
                   "  --stream    测试流式 API\n"
                   "  -o DIR      输出目录\n"
                   "  -v          详细\n");
            exit(0);
        }
    }
    return opts;
}

/* ── 计时工具 ──────────────────────────────────────────────────────── */

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1.0e9;
}

static void get_rusage_times(double *user, double *sys)
{
    struct rusage ru;
    getrusage(RUSAGE_CHILDREN, &ru);
    *user = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1.0e6;
    *sys  = ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1.0e6;
}

/* ── 生成测试帧 (NV12) ────────────────────────────────────────────── */

static rkvc_frame *generate_test_frame(int w, int h, int frame_idx)
{
    rkvc_frame *f = NULL;
    rkvc_err err = rkvc_frame_alloc(&f, w, h, RKVC_PIX_FMT_NV12);
    if (err != RKVC_OK)
        return NULL;

    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_get_data(f, planes, strides);

    /* Y 平面: 渐变 + 帧序号偏移 */
    if (planes[0]) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < strides[0]; x++) {
                planes[0][y * strides[0] + x] =
                    (uint8_t)((x + y + frame_idx) & 0xFF);
            }
        }
    }

    /* UV 平面 */
    if (planes[1]) {
        for (int y = 0; y < h / 2; y++) {
            for (int x = 0; x < strides[1]; x += 2) {
                planes[1][y * strides[1] + x]     = 128;
                planes[1][y * strides[1] + x + 1] = 128;
            }
        }
    }

    rkvc_frame_set_pts(f, frame_idx);
    return f;
}

/* ── 编码吞吐测试 ─────────────────────────────────────────────────── */

typedef struct {
    double elapsed;
    double fps;
    double realtime_x;
    double cpu_pct;
    uint64_t total_bytes;
    int frames_encoded;
    rkvc_err error;
} bench_result;

static bench_result run_encode_bench(const bench_opts *opts)
{
    bench_result res = {0};
    rkvc_err err;

    char outpath[512];
    snprintf(outpath, sizeof(outpath), "%s/bench_enc.h265", opts->output_dir);

    rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
    cfg.width   = opts->width;
    cfg.height  = opts->height;
    cfg.fps_num = opts->fps;
    cfg.fps_den = 1;
    cfg.bitrate = opts->bitrate;

    rkvc_encoder *enc = NULL;
    err = rkvc_encoder_open_file(&enc, &cfg, outpath);
    if (err != RKVC_OK) {
        res.error = err;
        fprintf(stderr, "编码器打开失败: %s\n", rkvc_err_str(err));
        return res;
    }

    double t0 = now_sec();

    for (int i = 0; i < opts->frames; i++) {
        rkvc_frame *f = generate_test_frame(opts->width, opts->height, i);
        if (!f) {
            res.error = RKVC_ERR_NOMEM;
            break;
        }

        err = rkvc_encoder_send_frame(enc, f);
        rkvc_frame_unref(f);

        if (err != RKVC_OK && err != RKVC_ERR_AGAIN) {
            res.error = err;
            break;
        }

        /* 取出编码包 (文件模式下 size 已被 muxer 消费, 仅计数帧) */
        rkvc_packet pkt;
        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
            res.frames_encoded++;
        }
    }

    /* drain */
    rkvc_encoder_drain(enc);
    rkvc_packet pkt;
    while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
        res.frames_encoded++;
    }

    res.elapsed = now_sec() - t0;

    rkvc_encoder_close(enc);

    /* 文件模式: 用文件大小作为编码数据量 */
    struct stat st;
    if (stat(outpath, &st) == 0)
        res.total_bytes = (uint64_t)st.st_size;

    if (res.elapsed > 0) {
        res.fps = res.frames_encoded / res.elapsed;
        res.realtime_x = res.fps / opts->fps;
    }

    return res;
}

/* ── 解码吞吐测试 ─────────────────────────────────────────────────── */

static bench_result run_decode_bench(const bench_opts *opts)
{
    bench_result res = {0};
    rkvc_err err;

    /* 先编码一个测试文件 */
    char encpath[512];
    snprintf(encpath, sizeof(encpath), "%s/bench_dec_src.h265", opts->output_dir);

    {
        rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
        cfg.width   = opts->width;
        cfg.height  = opts->height;
        cfg.fps_num = opts->fps;
        cfg.bitrate = opts->bitrate;

        rkvc_encoder *enc = NULL;
        err = rkvc_encoder_open_file(&enc, &cfg, encpath);
        if (err != RKVC_OK) {
            res.error = err;
            return res;
        }

        for (int i = 0; i < opts->frames; i++) {
            rkvc_frame *f = generate_test_frame(opts->width, opts->height, i);
            if (!f) break;
            rkvc_encoder_send_frame(enc, f);
            rkvc_frame_unref(f);
            rkvc_packet pkt;
            while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {}
        }
        rkvc_encoder_drain(enc);
        rkvc_packet pkt;
        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {}
        rkvc_encoder_close(enc);
    }

    /* 解码测试 */
    rkvc_decoder *dec = NULL;
    rkvc_decoder_config dcfg = rkvc_decoder_config_defaults();
    err = rkvc_decoder_open_file(&dec, &dcfg, encpath);
    if (err != RKVC_OK) {
        res.error = err;
        fprintf(stderr, "解码器打开失败: %s\n", rkvc_err_str(err));
        return res;
    }

    double t0 = now_sec();

    for (;;) {
        err = rkvc_decoder_read_packet(dec);
        if (err == RKVC_ERR_EOF)
            break;
        if (err != RKVC_OK)
            break;

        rkvc_frame *f = NULL;
        while (rkvc_decoder_receive_frame(dec, &f) == RKVC_OK) {
            rkvc_frame_info fi;
            rkvc_frame_get_info(f, &fi);
            res.frames_encoded++; /* 复用字段计数解码帧 */
            res.total_bytes += (uint64_t)fi.width * fi.height * 3 / 2;
            rkvc_frame_unref(f);
        }
    }

    res.elapsed = now_sec() - t0;
    rkvc_decoder_close(dec);

    if (res.elapsed > 0) {
        res.fps = res.frames_encoded / res.elapsed;
        res.realtime_x = res.fps / opts->fps;
    }

    return res;
}

/* ── 流式 API 测试 ────────────────────────────────────────────────── */

static bench_result run_stream_bench(const bench_opts *opts)
{
    bench_result res = {0};

    /* 直接用编码器 API 测试流式模式 (无文件输出) */
    rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
    cfg.width   = opts->width;
    cfg.height  = opts->height;
    cfg.fps_num = opts->fps;
    cfg.bitrate = opts->bitrate;

    rkvc_encoder *enc = NULL;
    rkvc_err err = rkvc_encoder_open(&enc, &cfg);
    if (err != RKVC_OK) {
        res.error = err;
        return res;
    }

    double t0 = now_sec();

    for (int i = 0; i < opts->frames; i++) {
        rkvc_frame *f = generate_test_frame(opts->width, opts->height, i);
        if (!f) break;

        err = rkvc_encoder_send_frame(enc, f);
        rkvc_frame_unref(f);

        /* drain until send succeeds */
        int retries = 0;
        while (err == RKVC_ERR_AGAIN && retries < 10000) {
            rkvc_packet pkt;
            while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
                res.frames_encoded++;
                res.total_bytes += pkt.size;
            }
            /* small yield to let MPP async finish */
            struct timespec ts = {0, 100000}; /* 100us */
            nanosleep(&ts, NULL);
            f = generate_test_frame(opts->width, opts->height, i);
            rkvc_frame_set_pts(f, i);
            err = rkvc_encoder_send_frame(enc, f);
            rkvc_frame_unref(f);
            retries++;
        }

        if (err != RKVC_OK && err != RKVC_ERR_AGAIN) {
            fprintf(stderr, "[rkvc] stream send_frame err=%d frame=%d\n", err, i);
            break;
        }

        if ((i + 1) % 50 == 0) {
            rkvc_packet pkt;
            while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
                res.frames_encoded++;
                res.total_bytes += pkt.size;
            }
        }
    }

    /* drain */
    rkvc_encoder_drain(enc);
    rkvc_packet pkt;
    while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
        res.frames_encoded++;
        res.total_bytes += pkt.size;
    }

    res.elapsed = now_sec() - t0;
    if (res.elapsed > 0) {
        res.fps = res.frames_encoded / res.elapsed;
        res.realtime_x = res.fps / opts->fps;
    }

    rkvc_encoder_close(enc);
    return res;
}

/* ── 输出结果 ──────────────────────────────────────────────────────── */

static void print_result(const char *label, const bench_result *res,
                         const bench_opts *opts)
{
    if (res->error != RKVC_OK) {
        printf("[FAIL] %-12s  %s\n", label, rkvc_err_str(res->error));
        return;
    }

    double bpp = 0;
    if (res->frames_encoded > 0 && opts->width > 0 && opts->height > 0)
        bpp = (double)(res->total_bytes * 8) /
              (opts->width * opts->height * res->frames_encoded);

    printf("[PASS] %-12s  %dx%d@%dfps  %d frames in %.3fs  "
           "%.1f fps  %.2fx realtime  %.4f bpp\n",
           label, opts->width, opts->height, opts->fps,
           res->frames_encoded, res->elapsed,
           res->fps, res->realtime_x, bpp);
}

static void write_tsv_header(FILE *fp)
{
    fprintf(fp, "test\tsize\trate\tframes\telapsed_s\tfps\trealtime\tbpp\ttotal_bytes\n");
}

static void write_tsv_row(FILE *fp, const char *label,
                          const bench_result *res,
                          const bench_opts *opts)
{
    double bpp = 0;
    if (res->frames_encoded > 0 && opts->width > 0 && opts->height > 0)
        bpp = (double)(res->total_bytes * 8) /
              (opts->width * opts->height * res->frames_encoded);

    fprintf(fp, "%s\t%dx%d\t%d\t%d\t%.3f\t%.1f\t%.2f\t%.4f\t%lu\n",
            label,
            opts->width, opts->height, opts->fps,
            res->frames_encoded, res->elapsed,
            res->fps, res->realtime_x, bpp,
            (unsigned long)res->total_bytes);
}

/* ── main ──────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    bench_opts opts = parse_args(argc, argv);

    /* 创建输出目录 */
    mkdir(opts.output_dir, 0755);

    /* 初始化库 */
    rkvc_err err = rkvc_init();
    if (err != RKVC_OK) {
        fprintf(stderr, "rkvc_init 失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    printf("rkvc_bench v%s\n", rkvc_version());
    printf("分辨率: %dx%d  帧率: %d  帧数: %d  码率: %d bps\n",
           opts.width, opts.height, opts.fps, opts.frames, opts.bitrate);

    /* 查询硬件能力 */
    rkvc_caps caps;
    if (rkvc_query_caps(&caps) == RKVC_OK) {
        printf("RKMPP 编码: %s  解码: %s  DMA: %s  RGA: %s\n",
               caps.has_rkmpp_enc ? "可用" : "不可用",
               caps.has_rkmpp_dec ? "可用" : "不可用",
               caps.has_dma_heap  ? "可用" : "不可用",
               caps.has_rga       ? "可用" : "不可用");
    }

    printf("\n");

    /* 打开 TSV 输出 */
    char tsv_path[512];
    snprintf(tsv_path, sizeof(tsv_path), "%s/summary.tsv", opts.output_dir);
    FILE *tsv = fopen(tsv_path, "w");
    if (tsv)
        write_tsv_header(tsv);

    /* 编码测试 */
    if (!opts.decode_only) {
        bench_result enc_res = run_encode_bench(&opts);
        print_result("encode", &enc_res, &opts);
        if (tsv) write_tsv_row(tsv, "encode", &enc_res, &opts);
    }

    /* 解码测试 */
    if (!opts.encode_only) {
        bench_result dec_res = run_decode_bench(&opts);
        print_result("decode", &dec_res, &opts);
        if (tsv) write_tsv_row(tsv, "decode", &dec_res, &opts);
    }

    /* 流式测试 */
    if (opts.test_stream) {
        bench_result strm_res = run_stream_bench(&opts);
        print_result("stream_enc", &strm_res, &opts);
        if (tsv) write_tsv_row(tsv, "stream_enc", &strm_res, &opts);
    }

    if (tsv) {
        fclose(tsv);
        printf("\n结果已写入: %s\n", tsv_path);
    }

    rkvc_deinit();
    return 0;
}
