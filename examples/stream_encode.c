/**
 * @file stream_encode.c
 * @brief 示例: 实时流式编码。
 *
 * 演示使用流式 API 进行实时编码，适合监控/推流场景。
 *
 * 用法:
 *   stream_encode [-s 1920x1080] [-r 30] [-n 300] [-b 4000000]
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <unistd.h>

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1.0e9;
}

int main(int argc, char **argv)
{
    int width = 1920, height = 1080, fps = 30, frames = 300;
    int bitrate = 4000000;
    int c;

    static struct option long_opts[] = {
        {"size",    required_argument, 0, 's'},
        {"rate",    required_argument, 0, 'r'},
        {"frames",  required_argument, 0, 'n'},
        {"bitrate", required_argument, 0, 'b'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "s:r:n:b:h", long_opts, NULL)) != -1) {
        switch (c) {
        case 's': sscanf(optarg, "%dx%d", &width, &height); break;
        case 'r': fps = atoi(optarg); break;
        case 'n': frames = atoi(optarg); break;
        case 'b': bitrate = atoi(optarg); break;
        case 'h':
            printf("stream_encode — 实时流式编码示例\n");
            return 0;
        }
    }

    rkvc_init();

    rkvc_stream_config scfg = rkvc_stream_config_defaults();
    scfg.direction = RKVC_STREAM_ENCODE;
    scfg.width     = width;
    scfg.height    = height;
    scfg.fps_num   = fps;
    scfg.bitrate   = bitrate;

    rkvc_stream *stream = NULL;
    rkvc_err err = rkvc_stream_open(&stream, &scfg);
    if (err != RKVC_OK) {
        fprintf(stderr, "打开流失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    printf("流式编码 %dx%d@%d, %d 帧\n", width, height, fps, frames);

    double frame_interval = 1.0 / fps;
    double t_start = now_sec();
    int encoded = 0;
    uint64_t total_bytes = 0;

    for (int i = 0; i < frames; i++) {
        /* 生成帧 */
        rkvc_frame *f = NULL;
        rkvc_frame_alloc(&f, width, height, RKVC_PIX_FMT_NV12);

        uint8_t *planes[4] = {0};
        int strides[4] = {0};
        rkvc_frame_get_data(f, planes, strides);

        /* 简单填充 */
        if (planes[0])
            memset(planes[0], (uint8_t)(i & 0xFF), width * height);
        if (planes[1])
            memset(planes[1], 128, width * height / 2);

        rkvc_frame_set_pts(f, i);

        /* Push */
        err = rkvc_stream_push(stream, f);
        rkvc_frame_unref(f);

        /* Pull 编码包 */
        rkvc_packet pkt;
        while (rkvc_stream_pull(stream, &pkt, 0) == RKVC_OK) {
            encoded++;
            total_bytes += pkt.size;
        }

        /* 模拟实时: 等待下一帧时间 */
        double target_time = t_start + (i + 1) * frame_interval;
        double sleep_time = target_time - now_sec();
        if (sleep_time > 0)
            usleep((unsigned int)(sleep_time * 1000000));

        if ((i + 1) % 100 == 0) {
            rkvc_stream_stats stats;
            rkvc_stream_get_stats(stream, &stats);
            printf("  帧 %d/%d, 编码 %d, 平均 %.1f fps\n",
                   i + 1, frames, encoded, stats.avg_fps);
        }
    }

    rkvc_stream_finish(stream);

    /* 取出剩余包 */
    rkvc_packet pkt;
    while (rkvc_stream_pull(stream, &pkt, -1) == RKVC_OK) {
        encoded++;
        total_bytes += pkt.size;
    }

    rkvc_stream_stats stats;
    rkvc_stream_get_stats(stream, &stats);

    double total_time = now_sec() - t_start;
    printf("\n流式编码完成:\n");
    printf("  编码帧数: %d\n", encoded);
    printf("  总时间: %.3fs\n", total_time);
    printf("  平均帧率: %.1f fps\n", stats.avg_fps);
    printf("  总数据量: %.2f KB\n", total_bytes / 1024.0);

    rkvc_stream_close(stream);
    rkvc_deinit();
    return 0;
}
