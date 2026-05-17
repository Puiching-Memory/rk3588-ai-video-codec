/**
 * @file stream_device_pair.c
 * @brief 示例: 模拟双设备流式传输（多种通道模式）。
 *
 * 模拟两台设备之间的实时视频传输，支持三种通道模式:
 *   ring — 环形缓冲区 (默认, 模拟 UDP 局域网)
 *   shm  — 共享内存 IPC (模拟 RTOS 零拷贝消息队列)
 *   rtp  — RTP/PS 封包 (模拟 GB/T 28181 国标传输)
 *
 * 用法:
 *   stream_device_pair -i input.h265 [-c ring|shm|rtp] [-s WxH] [-b BPS]
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1.0e9;
}

/* ════════════════════════════════════════════════════════════════════
 * 通道抽象层 (仅在本示例中使用，不暴露为库 API)
 * ════════════════════════════════════════════════════════════════════ */

typedef enum {
    CHAN_RING = 0,   /* 环形缓冲区 */
    CHAN_SHM  = 1,   /* 共享内存 IPC */
    CHAN_RTP  = 2,   /* RTP/PS 国标 */
} chan_type;

typedef struct {
    const uint8_t *data;
    int            size;
    int64_t        pts;
    int            key_frame;
} chan_frame;

/* 通道接口 */
typedef struct channel channel;
typedef void   (*chan_send_fn)(channel *ch, const chan_frame *f);
typedef int    (*chan_recv_fn)(channel *ch, chan_frame *out);
typedef void   (*chan_finish_fn)(channel *ch);
typedef void   (*chan_destroy_fn)(channel *ch);

struct channel {
    chan_type     type;
    const char   *name;
    chan_send_fn  send;
    chan_recv_fn  recv;
    chan_finish_fn finish;
    chan_destroy_fn destroy;

    /* 统计 */
    uint64_t pkts_sent;
    uint64_t pkts_recv;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    double   total_latency_ms;  /* 累计传输延迟 */
    int      latency_samples;
};

/* ── 模式 A: 环形缓冲区 (模拟 UDP 局域网) ───────────────────────── */

#define RING_CAP 128

typedef struct {
    channel   base;
    chan_frame slots[RING_CAP];
    int       head, tail, count;
    int       finished;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} ring_channel;

static void ring_send(channel *ch, const chan_frame *f)
{
    ring_channel *r = (ring_channel *)ch;
    pthread_mutex_lock(&r->lock);
    while (r->count >= RING_CAP)
        pthread_cond_wait(&r->not_full, &r->lock);

    r->slots[r->tail] = *f;
    r->slots[r->tail].data = malloc(f->size);
    memcpy((void *)r->slots[r->tail].data, f->data, f->size);

    r->tail = (r->tail + 1) % RING_CAP;
    r->count++;
    ch->pkts_sent++;
    ch->bytes_sent += f->size;

    pthread_cond_signal(&r->not_empty);
    pthread_mutex_unlock(&r->lock);
}

static int ring_recv(channel *ch, chan_frame *out)
{
    ring_channel *r = (ring_channel *)ch;
    pthread_mutex_lock(&r->lock);
    while (r->count == 0 && !r->finished)
        pthread_cond_wait(&r->not_empty, &r->lock);
    if (r->count == 0 && r->finished) {
        pthread_mutex_unlock(&r->lock);
        return -1;
    }

    *out = r->slots[r->head];
    r->head = (r->head + 1) % RING_CAP;
    r->count--;
    ch->pkts_recv++;
    ch->bytes_recv += out->size;

    pthread_cond_signal(&r->not_full);
    pthread_mutex_unlock(&r->lock);
    return 0;
}

static void ring_finish(channel *ch)
{
    ring_channel *r = (ring_channel *)ch;
    pthread_mutex_lock(&r->lock);
    r->finished = 1;
    pthread_cond_broadcast(&r->not_empty);
    pthread_mutex_unlock(&r->lock);
}

static void ring_destroy(channel *ch)
{
    ring_channel *r = (ring_channel *)ch;
    pthread_mutex_destroy(&r->lock);
    pthread_cond_destroy(&r->not_empty);
    pthread_cond_destroy(&r->not_full);
}

static channel *ring_create(void)
{
    ring_channel *r = calloc(1, sizeof(*r));
    r->base.type = CHAN_RING;
    r->base.name = "ring (UDP 局域网模拟)";
    r->base.send = ring_send;
    r->base.recv = ring_recv;
    r->base.finish = ring_finish;
    r->base.destroy = ring_destroy;
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->not_empty, NULL);
    pthread_cond_init(&r->not_full, NULL);
    return &r->base;
}

/* ── 模式 B: 共享内存 IPC (模拟 RTOS 消息队列) ──────────────────── */

/*
 * 模拟 RTOS 共享内存传输:
 *   - 固定大小的内存池，通过 mmap 共享
 *   - 使用 volatile 标志实现无锁同步（单生产者-单消费者）
 *   - 零拷贝：发送端写入 shm，接收端直接读取同一地址
 */

#define SHM_POOL_SIZE  (8 * 1024 * 1024)  /* 8 MB */
#define SHM_SLOTS      256

typedef struct {
    uint32_t offset;    /* 数据在 pool 中的偏移 */
    uint32_t size;
    int64_t  pts;
    int      key_frame;
    volatile uint32_t ready; /* 生产者写 1 = 数据就绪 */
} shm_slot;

typedef struct {
    channel    base;
    uint8_t   *pool;         /* 共享内存池 */
    uint32_t   pool_off;     /* 当前写入偏移 */
    shm_slot  *slots;        /* 共享 slot 数组 */
    volatile uint32_t head;  /* 消费者读指针 */
    volatile uint32_t tail;  /* 生产者写指针 */
    int        finished;
    char       shm_name[64];
} shm_channel;

static void shm_send(channel *ch, const chan_frame *f)
{
    shm_channel *s = (shm_channel *)ch;

    /* 等待 slot 有空间 (绕环) */
    while (((s->tail + 1) % SHM_SLOTS) == s->head) {
        struct timespec ts = {0, 100000}; /* 100us */
        nanosleep(&ts, NULL);
    }

    /* 分配 pool 空间 (环形分配) */
    if (s->pool_off + f->size > SHM_POOL_SIZE)
        s->pool_off = 0;

    /* 零拷贝写入: 直接写入共享内存 */
    memcpy(s->pool + s->pool_off, f->data, f->size);

    /* 填写 slot 元数据 */
    uint32_t idx = s->tail % SHM_SLOTS;
    s->slots[idx].offset = s->pool_off;
    s->slots[idx].size = f->size;
    s->slots[idx].pts = f->pts;
    s->slots[idx].key_frame = f->key_frame;
    __sync_synchronize(); /* 内存屏障 */
    s->slots[idx].ready = 1;

    s->pool_off += f->size;
    __sync_synchronize();
    s->tail++;

    ch->pkts_sent++;
    ch->bytes_sent += f->size;
}

static int shm_recv(channel *ch, chan_frame *out)
{
    shm_channel *s = (shm_channel *)ch;

    while (s->head == s->tail && !s->finished) {
        struct timespec ts = {0, 100000};
        nanosleep(&ts, NULL);
    }
    if (s->head == s->tail && s->finished)
        return -1;

    uint32_t idx = s->head % SHM_SLOTS;

    /* 等待 slot ready */
    while (!s->slots[idx].ready) {
        struct timespec ts = {0, 10000};
        nanosleep(&ts, NULL);
    }

    /* 拷贝出来（接收端需要 free，不能直接引用 shm） */
    uint8_t *copy = malloc(s->slots[idx].size);
    memcpy(copy, s->pool + s->slots[idx].offset, s->slots[idx].size);
    out->data = copy;
    out->size = s->slots[idx].size;
    out->pts = s->slots[idx].pts;
    out->key_frame = s->slots[idx].key_frame;

    s->slots[idx].ready = 0;
    __sync_synchronize();
    s->head++;

    ch->pkts_recv++;
    ch->bytes_recv += out->size;
    return 0;
}

static void shm_finish(channel *ch)
{
    shm_channel *s = (shm_channel *)ch;
    __sync_synchronize();
    s->finished = 1;
}

static void shm_destroy(channel *ch)
{
    shm_channel *s = (shm_channel *)ch;
    munmap(s->pool, SHM_POOL_SIZE);
    munmap(s->slots, SHM_SLOTS * sizeof(shm_slot));
}

static channel *shm_create(void)
{
    shm_channel *s = calloc(1, sizeof(*s));
    s->base.type = CHAN_SHM;
    s->base.name = "shm (RTOS 共享内存零拷贝)";
    s->base.send = shm_send;
    s->base.recv = shm_recv;
    s->base.finish = shm_finish;
    s->base.destroy = shm_destroy;

    /* 匿名 mmap 模拟共享内存 */
    s->pool = mmap(NULL, SHM_POOL_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    s->slots = mmap(NULL, SHM_SLOTS * sizeof(shm_slot), PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(s->slots, 0, SHM_SLOTS * sizeof(shm_slot));

    return &s->base;
}

/* ── 模式 C: RTP 封包 (模拟 GB/T 28181 国标传输) ──────────────────── */

/*
 * GB/T 28181 传输层模拟:
 *   - H.265 NAL 分片为 RTP 包 (≤ 1400 bytes payload)
 *   - RTP V=2, marker bit 标记帧边界
 *   - 大帧自动分片，接收端重组
 *   - 模拟 UDP 传输的包序和分片行为
 */

#define RTP_PAYLOAD_MAX 1400
#define RTP_RING_CAP    512

typedef struct {
    uint8_t *data;
    int      size;
} rtp_pkt;

typedef struct {
    channel   base;
    rtp_pkt   ring[RTP_RING_CAP];
    volatile int head, tail, count;
    volatile int finished;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} rtp_channel;

static void rtp_send(channel *ch, const chan_frame *f)
{
    rtp_channel *r = (rtp_channel *)ch;

    /* 分片为 RTP 包 */
    int total = f->size;
    int seq = 0;
    for (int pos = 0; pos < total; pos += RTP_PAYLOAD_MAX) {
        int chunk = total - pos;
        if (chunk > RTP_PAYLOAD_MAX) chunk = RTP_PAYLOAD_MAX;
        int is_last = (pos + chunk >= total);

        /* RTP header (12 bytes) */
        int rtp_len = 12 + chunk;
        uint8_t *rtp_buf = malloc(rtp_len);

        rtp_buf[0] = 0x80; /* V=2 */
        rtp_buf[1] = is_last ? (0x80 | 96) : 96; /* marker(bit7) + PT=96 */
        rtp_buf[2] = (seq >> 8) & 0xFF;
        rtp_buf[3] = seq & 0xFF;
        /* timestamp */
        int64_t ts = f->pts & 0xFFFFFFFF;
        rtp_buf[4] = (ts >> 24) & 0xFF;
        rtp_buf[5] = (ts >> 16) & 0xFF;
        rtp_buf[6] = (ts >>  8) & 0xFF;
        rtp_buf[7] = (ts >>  0) & 0xFF;
        /* SSRC */
        rtp_buf[8]  = 0x01; rtp_buf[9]  = 0x02;
        rtp_buf[10] = 0x03; rtp_buf[11] = 0x04;

        memcpy(rtp_buf + 12, f->data + pos, chunk);

        /* 发送到 RTP 环形缓冲 */
        pthread_mutex_lock(&r->lock);
        while (r->count >= RTP_RING_CAP)
            pthread_cond_wait(&r->not_full, &r->lock);

        r->ring[r->tail].data = rtp_buf;
        r->ring[r->tail].size = rtp_len;
        r->tail = (r->tail + 1) % RTP_RING_CAP;
        r->count++;
        ch->bytes_sent += rtp_len;

        pthread_cond_signal(&r->not_empty);
        pthread_mutex_unlock(&r->lock);

        seq++;
    }

    ch->pkts_sent++;
}

static int rtp_recv(channel *ch, chan_frame *out)
{
    rtp_channel *r = (rtp_channel *)ch;

    uint8_t *frame_buf = NULL;
    int frame_size = 0;
    int64_t pts = 0;
    int got_marker = 0;

    while (!got_marker) {
        while (r->count == 0 && !r->finished) {
            struct timespec ts = {0, 100000};
            nanosleep(&ts, NULL);
        }
        if (r->count == 0 && r->finished) {
            if (frame_size == 0) return -1;
            break;
        }

        pthread_mutex_lock(&r->lock);
        if (r->count == 0) {
            pthread_mutex_unlock(&r->lock);
            continue;
        }
        rtp_pkt pkt = r->ring[r->head];
        r->head = (r->head + 1) % RTP_RING_CAP;
        r->count--;
        ch->bytes_recv += pkt.size;
        pthread_cond_signal(&r->not_full);
        pthread_mutex_unlock(&r->lock);

        if (pkt.size > 12) {
            pts = ((int64_t)pkt.data[4] << 24) |
                  ((int64_t)pkt.data[5] << 16) |
                  ((int64_t)pkt.data[6] << 8)  |
                  ((int64_t)pkt.data[7]);
            int marker = pkt.data[1] & 0x80;
            int payload_len = pkt.size - 12;
            frame_buf = realloc(frame_buf, frame_size + payload_len);
            memcpy(frame_buf + frame_size, pkt.data + 12, payload_len);
            frame_size += payload_len;
            if (marker) got_marker = 1;
        }
        free(pkt.data);
    }

    out->data = frame_buf;
    out->size = frame_size;
    out->pts = pts;
    out->key_frame = 0;
    ch->pkts_recv++;
    return 0;
}

static void rtp_finish(channel *ch)
{
    rtp_channel *r = (rtp_channel *)ch;
    pthread_mutex_lock(&r->lock);
    r->finished = 1;
    pthread_cond_broadcast(&r->not_empty);
    pthread_mutex_unlock(&r->lock);
}

static void rtp_destroy(channel *ch)
{
    rtp_channel *r = (rtp_channel *)ch;
    /* 清空剩余包 */
    while (r->count > 0) {
        free(r->ring[r->head].data);
        r->head = (r->head + 1) % RTP_RING_CAP;
        r->count--;
    }
    pthread_mutex_destroy(&r->lock);
    pthread_cond_destroy(&r->not_empty);
    pthread_cond_destroy(&r->not_full);
}

static channel *rtp_create(void)
{
    rtp_channel *r = calloc(1, sizeof(*r));
    r->base.type = CHAN_RTP;
    r->base.name = "rtp (GB/T 28181 PS/RTP)";
    r->base.send = rtp_send;
    r->base.recv = rtp_recv;
    r->base.finish = rtp_finish;
    r->base.destroy = rtp_destroy;
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->not_empty, NULL);
    pthread_cond_init(&r->not_full, NULL);
    return &r->base;
}

/* ════════════════════════════════════════════════════════════════════
 * 设备 A: 发送端线程
 * ════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char   *input;
    int           out_width, out_height, out_bitrate;
    channel      *chan;
    int           frames_sent;
    double        elapsed;
} sender_ctx;

static void *sender_thread(void *arg)
{
    sender_ctx *ctx = arg;

    rkvc_decoder *dec = NULL;
    rkvc_decoder_config dcfg = rkvc_decoder_config_defaults();
    rkvc_err err = rkvc_decoder_open_file(&dec, &dcfg, ctx->input);
    if (err != RKVC_OK) {
        fprintf(stderr, "[设备A] 打开解码器失败: %s\n", rkvc_err_str(err));
        ctx->chan->finish(ctx->chan);
        return NULL;
    }

    int src_w = 0, src_h = 0, fps_num = 30, fps_den = 1;
    rkvc_decoder_get_video_info(dec, &src_w, &src_h, &fps_num, &fps_den);
    printf("[设备A] 输入 %dx%d @ %d fps → %dx%d @ %d kbps\n",
           src_w, src_h, fps_num,
           ctx->out_width, ctx->out_height, ctx->out_bitrate / 1000);

    rkvc_stream_config scfg = rkvc_stream_config_defaults();
    scfg.direction = RKVC_STREAM_ENCODE;
    scfg.width     = ctx->out_width;
    scfg.height    = ctx->out_height;
    scfg.fps_num   = fps_num;
    scfg.fps_den   = fps_den;
    scfg.bitrate   = ctx->out_bitrate;

    rkvc_stream *enc = NULL;
    err = rkvc_stream_open(&enc, &scfg);
    if (err != RKVC_OK) {
        fprintf(stderr, "[设备A] 打开编码流失败: %s\n", rkvc_err_str(err));
        rkvc_decoder_close(dec);
        ctx->chan->finish(ctx->chan);
        return NULL;
    }

    double t0 = now_sec();
    double frame_interval = (double)fps_den / fps_num;
    int count = 0;

    for (;;) {
        err = rkvc_decoder_read_packet(dec);
        if (err == RKVC_ERR_EOF) break;
        if (err != RKVC_OK) break;

        rkvc_frame *f = NULL;
        while (rkvc_decoder_receive_frame(dec, &f) == RKVC_OK) {
            err = rkvc_stream_push(enc, f);
            rkvc_frame_unref(f);

            if (err != RKVC_OK && err != RKVC_ERR_AGAIN) {
                fprintf(stderr, "[设备A] 编码失败: %s\n", rkvc_err_str(err));
                break;
            }

            rkvc_packet pkt;
            while (rkvc_stream_pull(enc, &pkt, 0) == RKVC_OK) {
                chan_frame cf = {
                    .data = pkt.data,
                    .size = pkt.size,
                    .pts = pkt.pts,
                    .key_frame = pkt.key_frame,
                };
                ctx->chan->send(ctx->chan, &cf);
            }
            count++;

            double target = t0 + count * frame_interval;
            double sleep_s = target - now_sec();
            if (sleep_s > 0)
                usleep((unsigned)(sleep_s * 1e6));
        }
    }

    rkvc_stream_finish(enc);
    rkvc_packet pkt;
    while (rkvc_stream_pull(enc, &pkt, -1) == RKVC_OK) {
        chan_frame cf = {pkt.data, pkt.size, pkt.pts, pkt.key_frame};
        ctx->chan->send(ctx->chan, &cf);
    }

    ctx->frames_sent = count;
    ctx->elapsed = now_sec() - t0;

    rkvc_stream_stats stats;
    rkvc_stream_get_stats(enc, &stats);
    printf("[设备A] 发送完成: %d 帧, %lu RTP包, %.2f KB\n",
           count, (unsigned long)ctx->chan->pkts_sent,
           ctx->chan->bytes_sent / 1024.0);

    rkvc_stream_close(enc);
    rkvc_decoder_close(dec);
    ctx->chan->finish(ctx->chan);
    return NULL;
}

/* ════════════════════════════════════════════════════════════════════
 * 设备 B: 接收端线程
 * ════════════════════════════════════════════════════════════════════ */

typedef struct {
    channel      *chan;
    int           frames_recv;
    double        elapsed;
} receiver_ctx;

static void *receiver_thread(void *arg)
{
    receiver_ctx *ctx = arg;

    rkvc_stream_config scfg = rkvc_stream_config_defaults();
    scfg.direction     = RKVC_STREAM_DECODE;
    scfg.output_format = RKVC_PIX_FMT_NV12;

    rkvc_stream *dec = NULL;
    rkvc_err err = rkvc_stream_open(&dec, &scfg);
    if (err != RKVC_OK) {
        fprintf(stderr, "[设备B] 打开解码流失败: %s\n", rkvc_err_str(err));
        return NULL;
    }

    double t0 = now_sec();
    int count = 0;
    double last_report = t0;

    for (;;) {
        chan_frame cf = {0};
        int rc = ctx->chan->recv(ctx->chan, &cf);
        if (rc != 0)
            break;

        double recv_time = now_sec();

        rkvc_packet pkt = {
            .data = cf.data,
            .size = cf.size,
            .pts = cf.pts,
            .dts = cf.pts,
            .key_frame = cf.key_frame,
        };
        err = rkvc_stream_push(dec, &pkt);
        if (cf.data) free((void *)cf.data);

        if (err != RKVC_OK && err != RKVC_ERR_AGAIN)
            break;

        rkvc_frame *f = NULL;
        while (rkvc_stream_pull(dec, &f, 0) == RKVC_OK) {
            count++;
            rkvc_frame_unref(f);
        }

        double now = now_sec();
        if (now - last_report >= 1.0) {
            printf("[设备B] 已解码 %d 帧 (%.1f fps)\n",
                   count, count / (now - (last_report - 1.0)));
            last_report = now;
        }
    }

    rkvc_stream_finish(dec);
    rkvc_frame *f = NULL;
    while (rkvc_stream_pull(dec, &f, -1) == RKVC_OK) {
        count++;
        rkvc_frame_unref(f);
    }

    ctx->frames_recv = count;
    ctx->elapsed = now_sec() - t0;

    rkvc_stream_stats stats;
    rkvc_stream_get_stats(dec, &stats);
    printf("[设备B] 接收完成: %d 帧, %lu 包, %.1f fps, 延迟 %.1f ms\n",
           count, (unsigned long)ctx->chan->pkts_recv,
           stats.avg_fps, stats.avg_latency_ms);

    rkvc_stream_close(dec);
    return NULL;
}

/* ════════════════════════════════════════════════════════════════════
 * 主程序
 * ════════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv)
{
    const char *input = NULL;
    const char *chan_name = "ring";
    int out_width = 1280, out_height = 720;
    int out_bitrate = 2000000;
    int c;

    static struct option long_opts[] = {
        {"input",   required_argument, 0, 'i'},
        {"channel", required_argument, 0, 'c'},
        {"size",    required_argument, 0, 's'},
        {"bitrate", required_argument, 0, 'b'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "i:c:s:b:h", long_opts, NULL)) != -1) {
        switch (c) {
        case 'i': input = optarg; break;
        case 'c': chan_name = optarg; break;
        case 's': sscanf(optarg, "%dx%d", &out_width, &out_height); break;
        case 'b': out_bitrate = atoi(optarg); break;
        case 'h':
            printf("stream_device_pair — 双设备流式传输模拟\n\n"
                   "  -i FILE       输入文件\n"
                   "  -c MODE       通道模式:\n"
                   "                  ring — 环形缓冲区 (默认, 模拟 UDP 局域网)\n"
                   "                  shm  — 共享内存 (RTOS 零拷贝消息队列)\n"
                   "                  rtp  — RTP/PS 封包 (GB/T 28181 国标)\n"
                   "  -s WxH        传输分辨率 (默认 1280x720)\n"
                   "  -b BPS        传输码率 (默认 2M)\n");
            return 0;
        }
    }

    if (!input) {
        fprintf(stderr, "需要指定输入文件: -i <file>\n");
        return 1;
    }

    /* 创建通道 */
    channel *ch = NULL;
    if (strcmp(chan_name, "ring") == 0)
        ch = ring_create();
    else if (strcmp(chan_name, "shm") == 0)
        ch = shm_create();
    else if (strcmp(chan_name, "rtp") == 0)
        ch = rtp_create();
    else {
        fprintf(stderr, "未知通道模式: %s (支持 ring/shm/rtp)\n", chan_name);
        return 1;
    }

    rkvc_init();

    printf("══════════════════════════════════════════\n");
    printf("  双设备流式传输模拟\n");
    printf("  输入:   %s\n", input);
    printf("  传输:   %dx%d @ %d kbps\n",
           out_width, out_height, out_bitrate / 1000);
    printf("  通道:   %s\n", ch->name);
    printf("══════════════════════════════════════════\n\n");

    double t_start = now_sec();

    sender_ctx sctx = {
        .input = input,
        .out_width = out_width, .out_height = out_height,
        .out_bitrate = out_bitrate,
        .chan = ch,
    };
    receiver_ctx rctx = { .chan = ch };

    pthread_t sender_tid, receiver_tid;
    pthread_create(&sender_tid, NULL, sender_thread, &sctx);
    pthread_create(&receiver_tid, NULL, receiver_thread, &rctx);

    pthread_join(sender_tid, NULL);
    pthread_join(receiver_tid, NULL);

    double t_total = now_sec() - t_start;

    printf("\n══════════════════════════════════════════\n");
    printf("  端到端统计\n");
    printf("──────────────────────────────────────────\n");
    printf("  通道模式: %s\n", ch->name);
    printf("  发送端:   %d 帧, %.3fs, %.1f fps\n",
           sctx.frames_sent, sctx.elapsed,
           sctx.elapsed > 0 ? sctx.frames_sent / sctx.elapsed : 0);
    printf("  接收端:   %d 帧, %.3fs, %.1f fps\n",
           rctx.frames_recv, rctx.elapsed,
           rctx.elapsed > 0 ? rctx.frames_recv / rctx.elapsed : 0);
    printf("  总耗时:   %.3fs\n", t_total);
    printf("  通道包数: %lu 发 / %lu 收\n",
           (unsigned long)ch->pkts_sent, (unsigned long)ch->pkts_recv);
    printf("  通道数据: %.2f KB 发 / %.2f KB 收\n",
           ch->bytes_sent / 1024.0, ch->bytes_recv / 1024.0);
    if (ch->type == CHAN_RTP) {
        double overhead = ch->bytes_sent > 0 ?
            (double)(ch->bytes_sent - ch->bytes_recv) / ch->bytes_sent * 100 : 0;
        printf("  RTP 开销: %.1f%% (RTP 头 + 分片)\n", overhead);
    }
    if (ch->type == CHAN_SHM)
        printf("  SHM 池:   %d MB (零拷贝)\n", SHM_POOL_SIZE / (1024 * 1024));
    printf("  帧完整:   %s\n",
           sctx.frames_sent == rctx.frames_recv ? "是" : "否");
    printf("══════════════════════════════════════════\n");

    ch->destroy(ch);
    free(ch);
    rkvc_deinit();
    return 0;
}
