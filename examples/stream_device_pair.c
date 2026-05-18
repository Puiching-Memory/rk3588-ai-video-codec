/**
 * @file stream_device_pair.c
 * @brief 示例: 双设备流式传输 (真实网络)。
 *
 * 支持两种真实网络传输模式:
 *   udp — 原始编码帧 over UDP (16B头: frag_id+frag_total+len+pts, 大帧自动分片)
 *   rtp — RTP 封包 over UDP (GB/T 28181, H.265 NAL 分片 ≤1400B)
 *
 * 用法:
 *   # 本机 test
 *   stream_device_pair -i input.h265 -c udp -r both --dst-ip 127.0.0.1
 *   stream_device_pair -i input.h265 -c rtp -r both --dst-ip 127.0.0.1
 *
 *   # 双设备部署
 *   # 设备 A (发送端):
 *   stream_device_pair -i input.h265 -c udp -r send --dst-ip <接收端IP> --dst-port 9000
 *   # 设备 B (接收端):
 *   stream_device_pair -c udp -r recv --bind-port 9000
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1.0e9;
}

/* ════════════════════════════════════════════════════════════════════
 * 通道抽象层
 * ════════════════════════════════════════════════════════════════════ */

typedef enum {
    CHAN_UDP = 0,   /* 原始编码帧 over UDP */
    CHAN_RTP = 1,   /* RTP 封包 over UDP (GB/T 28181) */
} chan_type;

typedef struct {
    const uint8_t *data;
    int            size;
    int64_t        pts;
    int            key_frame;
} chan_frame;

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

    uint64_t pkts_sent;
    uint64_t pkts_recv;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
};

/* ════════════════════════════════════════════════════════════════════
 * 通用 UDP Socket 辅助
 * ════════════════════════════════════════════════════════════════════ */

static int udp_socket_create(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("[SOCK] socket");
        return -1;
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    return fd;
}

static int udp_socket_bind(int fd, int port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[SOCK] bind");
        return -1;
    }
    return 0;
}

static int udp_set_dst(struct sockaddr_in *dst, const char *ip, int port)
{
    memset(dst, 0, sizeof(*dst));
    dst->sin_family = AF_INET;
    dst->sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &dst->sin_addr) != 1) {
        fprintf(stderr, "[SOCK] 无效 IP: %s\n", ip);
        return -1;
    }
    return 0;
}

static ssize_t udp_send_raw(int fd, const struct sockaddr_in *dst,
                            const void *data, size_t len)
{
    return sendto(fd, data, len, 0, (const struct sockaddr *)dst, sizeof(*dst));
}

static ssize_t udp_recv_raw(int fd, void *buf, size_t bufsz,
                            struct sockaddr_in *src)
{
    socklen_t srclen = sizeof(*src);
    return recvfrom(fd, buf, bufsz, 0, (struct sockaddr *)src, &srclen);
}

/* ════════════════════════════════════════════════════════════════════
 * 模式 A: 原始编码帧 over UDP
 * ════════════════════════════════════════════════════════════════════
 *
 * 帧封装: 16B 头 + 数据分片
 *   头结构: uint16 frag_id + uint16 frag_total + uint32 frame_len + uint64 pts
 *   大帧自动分片 (IDR 帧可达 100+KB), 接收端按 PTS 缓冲重组
 */

#define UDP_FRAG_HEADER  16   /* frag_id(2) + frag_total(2) + frame_len(4) + pts(8) */
#define UDP_FRAG_PAYLOAD 65491 /* 65507 - 16 */
#define UDP_MAX_PAYLOAD   65507
#define UDP_MAX_FRAGS     16

typedef struct {
    uint8_t *data;
    int      size;
    int      capacity;
    int      frag_total;
    uint8_t  frag_mask[2];  /* bitmap: frag_mask[i/8] |= (1 << (i%8)) */
    int64_t  pts;
} udp_reasm;

typedef struct {
    channel          base;
    int              send_fd;
    int              recv_fd;
    struct sockaddr_in dst_addr;
    int              has_dst;
    udp_reasm        reasm;
} udp_channel;

static void udp_send_one(udp_channel *u, int frag_id, int frag_total,
                         uint32_t frame_len, int64_t pts,
                         const uint8_t *data, int len)
{
    uint8_t hdr[UDP_FRAG_HEADER];
    uint16_t net_frag_id    = htons((uint16_t)frag_id);
    uint16_t net_frag_total = htons((uint16_t)frag_total);
    uint32_t net_frame_len  = htonl(frame_len);
    uint64_t net_pts        = htobe64((uint64_t)pts);
    memcpy(hdr,      &net_frag_id,    2);
    memcpy(hdr + 2,  &net_frag_total, 2);
    memcpy(hdr + 4,  &net_frame_len,  4);
    memcpy(hdr + 8,  &net_pts,        8);

    struct iovec iov[2] = {
        { .iov_base = hdr,       .iov_len = UDP_FRAG_HEADER },
        { .iov_base = (void *)data, .iov_len = (size_t)len },
    };
    struct msghdr msg = {
        .msg_name    = &u->dst_addr,
        .msg_namelen = sizeof(u->dst_addr),
        .msg_iov     = iov,
        .msg_iovlen  = 2,
    };

    ssize_t sent = sendmsg(u->send_fd, &msg, 0);
    if (sent > 0) {
        u->base.bytes_sent += (uint64_t)sent;
    } else {
        perror("[UDP] sendmsg");
    }
}

static void udp_send(channel *ch, const chan_frame *f)
{
    udp_channel *u = (udp_channel *)ch;
    if (u->send_fd < 0 || !u->has_dst) return;

    int total    = f->size;
    int n_frags  = (total + UDP_FRAG_PAYLOAD - 1) / UDP_FRAG_PAYLOAD;
    if (n_frags > UDP_MAX_FRAGS) {
        fprintf(stderr, "[UDP] 帧过大需 %d 分片 (>%d), 丢弃\n",
                n_frags, UDP_MAX_FRAGS);
        return;
    }

    if (n_frags > 1)
        printf("[UDP] 大帧分片: %d bytes → %d 片\n", total, n_frags);

    for (int i = 0, off = 0; i < n_frags; i++) {
        int chunk = total - off;
        if (chunk > UDP_FRAG_PAYLOAD) chunk = UDP_FRAG_PAYLOAD;
        udp_send_one(u, i, n_frags, (uint32_t)total, f->pts,
                     f->data + off, chunk);
        off += chunk;
    }
    ch->pkts_sent++;
}

static void reasm_reset(udp_reasm *r, int frag_total, int frame_len, int64_t pts)
{
    if (r->capacity < frame_len) {
        free(r->data);
        r->data     = malloc(frame_len);
        r->capacity = frame_len;
    }
    r->size       = 0;
    r->frag_total = frag_total;
    r->pts        = pts;
    memset(r->frag_mask, 0, sizeof(r->frag_mask));
}

static int reasm_complete(const udp_reasm *r)
{
    if (r->frag_total <= 0) return 0;
    for (int i = 0; i < r->frag_total; i++) {
        if (!(r->frag_mask[i / 8] & (1 << (i % 8)))) return 0;
    }
    return 1;
}

static int udp_recv(channel *ch, chan_frame *out)
{
    udp_channel *u = (udp_channel *)ch;
    if (u->recv_fd < 0) return -1;

    uint8_t buf[UDP_MAX_PAYLOAD];
    struct sockaddr_in src;

    for (;;) {
        ssize_t n = udp_recv_raw(u->recv_fd, buf, sizeof(buf), &src);
        if (n < 0) {
            perror("[UDP] recvfrom");
            return -1;
        }
        if (n <= UDP_FRAG_HEADER) {
            /* 空帧 = 结束信号 */
            return -1;
        }

        uint16_t frag_id, frag_total;
        uint32_t frame_len;
        int64_t  pts;
        memcpy(&frag_id,    buf,     2); frag_id    = ntohs(frag_id);
        memcpy(&frag_total, buf + 2, 2); frag_total = ntohs(frag_total);
        memcpy(&frame_len,  buf + 4, 4); frame_len  = ntohl(frame_len);
        memcpy(&pts,        buf + 8, 8); pts         = (int64_t)be64toh(pts);

        int payload_len = (int)n - UDP_FRAG_HEADER;
        if (payload_len <= 0) continue;

        udp_reasm *r = &u->reasm;

        /* 新帧开始: 重置组装缓冲 */
        if (frag_id == 0)
            reasm_reset(r, frag_total, (int)frame_len, pts);

        /* 忽略无法匹配的碎片 */
        if (r->frag_total != frag_total || r->pts != pts)
            continue;
        if (frag_id >= frag_total)
            continue;

        /* 去重: 已收到则跳过 */
        if (r->frag_mask[frag_id / 8] & (1 << (frag_id % 8)))
            continue;

        /* 写入组装缓冲 */
        int offset = frag_id * UDP_FRAG_PAYLOAD;
        int copy_n = payload_len;
        if (offset + copy_n > r->capacity)
            copy_n = r->capacity - offset;
        if (copy_n > 0)
            memcpy(r->data + offset, buf + UDP_FRAG_HEADER, copy_n);
        r->size += copy_n;
        r->frag_mask[frag_id / 8] |= (1 << (frag_id % 8));

        ch->bytes_recv += (uint64_t)n;

        /* 检查组装完成 */
        if (reasm_complete(r)) {
            out->data      = r->data;
            out->size      = r->size;
            out->pts       = r->pts;
            out->key_frame = 0;
            ch->pkts_recv++;
            /* 释放所有权: 调用方负责 free */
            r->data     = NULL;
            r->capacity = 0;
            return 0;
        }
    }
}

static void udp_finish(channel *ch)
{
    udp_channel *u = (udp_channel *)ch;
    if (u->send_fd >= 0 && u->has_dst) {
        /* 发送全零头作为结束信号 (frag_total=0) */
        uint8_t zero[UDP_FRAG_HEADER];
        memset(zero, 0, sizeof(zero));
        udp_send_raw(u->send_fd, &u->dst_addr, zero, sizeof(zero));
    }
}

static void udp_destroy(channel *ch)
{
    udp_channel *u = (udp_channel *)ch;
    if (u->send_fd >= 0) close(u->send_fd);
    if (u->recv_fd >= 0) close(u->recv_fd);
    free(u->reasm.data);
}

static channel *udp_create(int is_sender, int is_receiver,
                           const char *dst_ip, int dst_port, int bind_port)
{
    udp_channel *u = calloc(1, sizeof(*u));
    if (!u) return NULL;
    u->base.type    = CHAN_UDP;
    u->base.name    = "udp (原始帧 over UDP)";
    u->base.send    = udp_send;
    u->base.recv    = udp_recv;
    u->base.finish  = udp_finish;
    u->base.destroy = udp_destroy;
    u->send_fd      = -1;
    u->recv_fd      = -1;

    if (is_sender) {
        u->send_fd = udp_socket_create();
        if (u->send_fd < 0) { free(u); return NULL; }
        if (udp_set_dst(&u->dst_addr, dst_ip, dst_port) < 0) {
            close(u->send_fd); free(u); return NULL;
        }
        u->has_dst = 1;
        printf("[UDP] 发送端就绪 → %s:%d\n", dst_ip, dst_port);
    }

    if (is_receiver) {
        u->recv_fd = udp_socket_create();
        if (u->recv_fd < 0) {
            if (u->send_fd >= 0) close(u->send_fd);
            free(u); return NULL;
        }
        if (udp_socket_bind(u->recv_fd, bind_port) < 0) {
            close(u->recv_fd);
            if (u->send_fd >= 0) close(u->send_fd);
            free(u); return NULL;
        }
        printf("[UDP] 接收端就绪 :%d\n", bind_port);
    }

    return &u->base;
}

/* ════════════════════════════════════════════════════════════════════
 * 模式 B: RTP 封包 over UDP (GB/T 28181)
 * ════════════════════════════════════════════════════════════════════
 *
 * H.265 NAL → RTP 分片 (≤1400B payload) → UDP Socket 发送
 * 接收端通过 UDP Socket 收包 → 重组完整帧
 */

#define RTP_PAYLOAD_MAX  1400
#define RTP_HEADER_SIZE  12

typedef struct {
    channel          base;
    int              send_fd;
    int              recv_fd;
    struct sockaddr_in dst_addr;
    int              has_dst;
    uint32_t         ssrc;
    int              finished;
} rtp_channel;

static void rtp_send(channel *ch, const chan_frame *f)
{
    rtp_channel *r = (rtp_channel *)ch;
    if (r->send_fd < 0 || !r->has_dst) return;

    int total = f->size;
    int seq   = 0;
    for (int pos = 0; pos < total; pos += RTP_PAYLOAD_MAX) {
        int chunk   = total - pos;
        if (chunk > RTP_PAYLOAD_MAX) chunk = RTP_PAYLOAD_MAX;
        int is_last = (pos + chunk >= total);

        uint8_t rtp_buf[RTP_HEADER_SIZE + RTP_PAYLOAD_MAX];
        int64_t ts_val = f->pts & 0xFFFFFFFF;

        rtp_buf[0] = 0x80; /* V=2 */
        rtp_buf[1] = is_last ? (0x80 | 96) : 96; /* M + PT=96 */
        rtp_buf[2] = (seq >> 8) & 0xFF;
        rtp_buf[3] = seq & 0xFF;
        rtp_buf[4] = (ts_val >> 24) & 0xFF;
        rtp_buf[5] = (ts_val >> 16) & 0xFF;
        rtp_buf[6] = (ts_val >>  8) & 0xFF;
        rtp_buf[7] = (ts_val >>  0) & 0xFF;
        rtp_buf[8]  = (r->ssrc >> 24) & 0xFF;
        rtp_buf[9]  = (r->ssrc >> 16) & 0xFF;
        rtp_buf[10] = (r->ssrc >>  8) & 0xFF;
        rtp_buf[11] = (r->ssrc >>  0) & 0xFF;

        int rtp_len = RTP_HEADER_SIZE + chunk;
        memcpy(rtp_buf + RTP_HEADER_SIZE, f->data + pos, chunk);

        ssize_t sent = udp_send_raw(r->send_fd, &r->dst_addr, rtp_buf, rtp_len);
        if (sent > 0) {
            ch->bytes_sent += (uint64_t)sent;
        } else {
            perror("[RTP] sendto");
        }
        seq++;
    }
    ch->pkts_sent++;
}

static int rtp_recv(channel *ch, chan_frame *out)
{
    rtp_channel *r = (rtp_channel *)ch;
    if (r->recv_fd < 0) return -1;

    uint8_t *frame_buf = NULL;
    int frame_size = 0;
    int64_t pts = 0;
    int got_marker = 0;

    uint8_t pkt_buf[UDP_MAX_PAYLOAD];
    struct sockaddr_in src;

    while (!got_marker) {
        ssize_t n = udp_recv_raw(r->recv_fd, pkt_buf, sizeof(pkt_buf), &src);
        if (n < 0) {
            if (r->finished && frame_size == 0) return -1;
            continue;
        }
        if (n <= RTP_HEADER_SIZE) continue;

        pts = ((int64_t)pkt_buf[4] << 24) |
              ((int64_t)pkt_buf[5] << 16) |
              ((int64_t)pkt_buf[6] << 8)  |
              ((int64_t)pkt_buf[7]);
        int marker      = pkt_buf[1] & 0x80;
        int payload_len = (int)n - RTP_HEADER_SIZE;

        ch->bytes_recv += (uint64_t)n;

        frame_buf = realloc(frame_buf, frame_size + payload_len);
        memcpy(frame_buf + frame_size, pkt_buf + RTP_HEADER_SIZE, payload_len);
        frame_size += payload_len;

        if (marker) got_marker = 1;
    }

    out->data      = frame_buf;
    out->size      = frame_size;
    out->pts       = pts;
    out->key_frame = 0;
    ch->pkts_recv++;
    return 0;
}

static void rtp_finish(channel *ch)
{
    rtp_channel *r = (rtp_channel *)ch;
    __sync_synchronize();
    r->finished = 1;
    /* 发送一个零负载的 RTP 终止包 */
    if (r->send_fd >= 0 && r->has_dst) {
        uint8_t fin[12] = {0x80, 0x80 | 96, 0, 0, 0, 0, 0, 0,
                           (r->ssrc>>24)&0xFF, (r->ssrc>>16)&0xFF,
                           (r->ssrc>>8)&0xFF, (r->ssrc>>0)&0xFF};
        udp_send_raw(r->send_fd, &r->dst_addr, fin, sizeof(fin));
    }
}

static void rtp_destroy(channel *ch)
{
    rtp_channel *r = (rtp_channel *)ch;
    if (r->send_fd >= 0) close(r->send_fd);
    if (r->recv_fd >= 0) close(r->recv_fd);
}

static channel *rtp_create(int is_sender, int is_receiver,
                           const char *dst_ip, int dst_port, int bind_port)
{
    rtp_channel *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->base.type    = CHAN_RTP;
    r->base.name    = "rtp (RTP over UDP, GB/T 28181)";
    r->base.send    = rtp_send;
    r->base.recv    = rtp_recv;
    r->base.finish  = rtp_finish;
    r->base.destroy = rtp_destroy;
    r->send_fd      = -1;
    r->recv_fd      = -1;
    r->ssrc         = 0x01020304;

    if (is_sender) {
        r->send_fd = udp_socket_create();
        if (r->send_fd < 0) { free(r); return NULL; }
        if (udp_set_dst(&r->dst_addr, dst_ip, dst_port) < 0) {
            close(r->send_fd); free(r); return NULL;
        }
        r->has_dst = 1;
        printf("[RTP] 发送端就绪 → %s:%d\n", dst_ip, dst_port);
    }

    if (is_receiver) {
        r->recv_fd = udp_socket_create();
        if (r->recv_fd < 0) {
            if (r->send_fd >= 0) close(r->send_fd);
            free(r); return NULL;
        }
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        setsockopt(r->recv_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (udp_socket_bind(r->recv_fd, bind_port) < 0) {
            close(r->recv_fd);
            if (r->send_fd >= 0) close(r->send_fd);
            free(r); return NULL;
        }
        printf("[RTP] 接收端就绪 :%d\n", bind_port);
    }

    return &r->base;
}

/* ════════════════════════════════════════════════════════════════════
 * 设备 A: 发送端线程
 * ════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *input;
    int         out_width, out_height, out_bitrate;
    channel    *chan;
    int         frames_sent;
    double      elapsed;
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
                    .data      = pkt.data,
                    .size      = pkt.size,
                    .pts       = pkt.pts,
                    .key_frame = pkt.key_frame,
                };
                ctx->chan->send(ctx->chan, &cf);
            }
            count++;

            double target  = t0 + count * frame_interval;
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
    ctx->elapsed     = now_sec() - t0;

    printf("[设备A] 发送完成: %d 帧, %lu 包, %.2f KB\n",
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
    channel *chan;
    int      frames_recv;
    double   elapsed;
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
        if (rc != 0) break;

        rkvc_packet pkt = {
            .data      = cf.data,
            .size      = cf.size,
            .pts       = cf.pts,
            .dts       = cf.pts,
            .key_frame = cf.key_frame,
        };
        err = rkvc_stream_push(dec, &pkt);
        if (cf.data) free((void *)cf.data);

        if (err != RKVC_OK && err != RKVC_ERR_AGAIN) break;

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
    ctx->elapsed     = now_sec() - t0;

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
    const char *input     = NULL;
    const char *chan_name = "udp";
    const char *role      = "both";
    const char *dst_ip    = "127.0.0.1";
    int dst_port  = 9000;
    int bind_port = 9000;
    int out_width = 1280, out_height = 720;
    int out_bitrate = 2000000;
    int c;

    static struct option long_opts[] = {
        {"input",     required_argument, 0, 'i'},
        {"channel",   required_argument, 0, 'c'},
        {"role",      required_argument, 0, 'r'},
        {"dst-ip",    required_argument, 0, 'D'},
        {"dst-port",  required_argument, 0, 'P'},
        {"bind-port", required_argument, 0, 'B'},
        {"size",      required_argument, 0, 's'},
        {"bitrate",   required_argument, 0, 'b'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "i:c:r:D:P:B:s:b:h", long_opts, NULL)) != -1) {
        switch (c) {
        case 'i': input      = optarg; break;
        case 'c': chan_name  = optarg; break;
        case 'r': role       = optarg; break;
        case 'D': dst_ip     = optarg; break;
        case 'P': dst_port   = atoi(optarg); break;
        case 'B': bind_port  = atoi(optarg); break;
        case 's': sscanf(optarg, "%dx%d", &out_width, &out_height); break;
        case 'b': out_bitrate = atoi(optarg); break;
        case 'h':
            printf("stream_device_pair — 双设备流式传输\n\n"
                   "用法:\n"
                   "  # 本机测试\n"
                   "  stream_device_pair -i input.h265 -c udp|rtp -r both --dst-ip 127.0.0.1\n\n"
                   "  # 双设备部署 (物理两台 RK3588)\n"
                   "  # 设备 A (发送端):\n"
                   "  stream_device_pair -i input.h265 -c udp|rtp -r send --dst-ip <接收端IP> --dst-port 9000\n"
                   "  # 设备 B (接收端):\n"
                   "  stream_device_pair -c udp|rtp -r recv --bind-port 9000\n\n"
                   "选项:\n"
                   "  -i FILE       输入文件 (接收端不需要)\n"
                   "  -c MODE       通道: udp (原始帧) / rtp (RTP 封包, GB/T 28181)\n"
                   "  -r ROLE       角色: send / recv / both (默认 both)\n"
                   "  --dst-ip IP   目标 IP (默认 127.0.0.1)\n"
                   "  --dst-port N  目标端口 (默认 9000)\n"
                   "  --bind-port N 绑定端口 (默认 9000)\n"
                   "  -s WxH        传输分辨率 (默认 1280x720)\n"
                   "  -b BPS        传输码率 (默认 2M)\n");
            return 0;
        }
    }

    int is_sender   = (strcmp(role, "send") == 0 || strcmp(role, "both") == 0);
    int is_receiver = (strcmp(role, "recv") == 0 || strcmp(role, "both") == 0);

    if (!is_sender && !is_receiver) {
        fprintf(stderr, "无效的角色: %s (支持 send/recv/both)\n", role);
        return 1;
    }
    if (is_sender && !input) {
        fprintf(stderr, "发送端需要指定输入文件: -i <file>\n");
        return 1;
    }

    channel *ch = NULL;
    if (strcmp(chan_name, "udp") == 0)
        ch = udp_create(is_sender, is_receiver, dst_ip, dst_port, bind_port);
    else if (strcmp(chan_name, "rtp") == 0)
        ch = rtp_create(is_sender, is_receiver, dst_ip, dst_port, bind_port);
    else {
        fprintf(stderr, "未知通道: %s (支持 udp/rtp)\n", chan_name);
        return 1;
    }
    if (!ch) return 1;

    rkvc_init();

    printf("══════════════════════════════════════════\n");
    printf("  双设备流式传输 (真实网络)\n");
    printf("  角色:    %s\n", role);
    printf("  通道:    %s\n", ch->name);
    if (is_sender) {
        printf("  输入:    %s\n", input);
        printf("  传输:    %dx%d @ %d kbps\n",
               out_width, out_height, out_bitrate / 1000);
        printf("  目标:    %s:%d\n", dst_ip, dst_port);
    }
    if (is_receiver)
        printf("  监听:    :%d\n", bind_port);
    printf("══════════════════════════════════════════\n\n");

    double t_start = now_sec();

    sender_ctx sctx = {
        .input       = input,
        .out_width   = out_width,
        .out_height  = out_height,
        .out_bitrate = out_bitrate,
        .chan        = ch,
    };
    receiver_ctx rctx = { .chan = ch };

    pthread_t sender_tid = 0, receiver_tid = 0;

    if (is_sender)
        pthread_create(&sender_tid, NULL, sender_thread, &sctx);
    if (is_receiver)
        pthread_create(&receiver_tid, NULL, receiver_thread, &rctx);

    if (sender_tid)
        pthread_join(sender_tid, NULL);
    if (receiver_tid)
        pthread_join(receiver_tid, NULL);

    double t_total = now_sec() - t_start;

    printf("\n══════════════════════════════════════════\n");
    printf("  端到端统计\n");
    printf("──────────────────────────────────────────\n");
    printf("  通道模式: %s\n", ch->name);
    if (is_sender)
        printf("  发送端:   %d 帧, %.3fs, %.1f fps\n",
               sctx.frames_sent, sctx.elapsed,
               sctx.elapsed > 0 ? sctx.frames_sent / sctx.elapsed : 0);
    if (is_receiver)
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
        printf("  RTP 开销: %.1f%% (RTP 头 + 分片 + UDP/IP 头)\n", overhead);
    }
    printf("  帧完整:   %s\n",
           (is_sender && is_receiver && sctx.frames_sent == rctx.frames_recv)
           ? "是" : (is_sender && is_receiver) ? "否" : "N/A (单角色)");
    printf("══════════════════════════════════════════\n");

    ch->destroy(ch);
    free(ch);
    rkvc_deinit();
    return 0;
}
