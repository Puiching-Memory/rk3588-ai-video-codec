/**
 * @file stream.h
 * @brief 实时流式处理 API。
 *
 * 提供面向实时场景的流式管线:
 *  - 编码流: 原始帧 → H265 压缩包 (适合 RTSP/RTMP 推流)
 *  - 解码流: H265 压缩包 → 原始帧 (适合实时监控解码)
 *  - 转码流: 原始帧 → 缩放/格式转换 → H265 压缩包
 *
 * 与 encoder/decoder API 的区别:
 *  - 流式 API 有内部环形缓冲区，支持异步 push/pull
 *  - 内置帧率控制和丢帧策略
 *  - 更适合多线程生产者-消费者模式
 */

#ifndef RKVC_STREAM_H
#define RKVC_STREAM_H

#include "rkvc/types.h"
#include "rkvc/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 流方向 ───────────────────────────────────────────────────────── */

typedef enum {
    RKVC_STREAM_ENCODE = 0,   /**< 原始帧 → 压缩包 */
    RKVC_STREAM_DECODE = 1,   /**< 压缩包 → 原始帧 */
} rkvc_stream_dir;

/* ── 流配置 ───────────────────────────────────────────────────────── */

typedef struct {
    rkvc_stream_dir direction;      /**< 流方向 (必填) */

    /* 编码参数 (direction == ENCODE 时有效) */
    int             width;          /**< 输入帧宽度 */
    int             height;         /**< 输入帧高度 */
    int             fps_num;        /**< 帧率分子 (默认 30) */
    int             fps_den;        /**< 帧率分母 (默认 1) */
    int64_t         bitrate;        /**< 目标码率 bps (默认 4M) */
    rkvc_pix_fmt    input_format;   /**< 输入像素格式 (默认 NV12) */
    rkvc_preset     preset;         /**< 编码预设 */

    /* 解码参数 (direction == DECODE 时有效) */
    rkvc_pix_fmt    output_format;  /**< 输出像素格式 (默认 NV12) */

    /* 通用参数 */
    int             buffer_size;    /**< 内部缓冲帧数 (默认 4) */
    int             drop_frames;    /**< 缓冲满时是否丢帧 (默认 0) */
} rkvc_stream_config;

/**
 * @brief 返回默认流配置。
 */
rkvc_stream_config rkvc_stream_config_defaults(void);

/* ── 流生命周期 ──────────────────────────────────────────────────── */

/** 不透明流句柄 */
typedef struct rkvc_stream rkvc_stream;

/**
 * @brief 打开流。
 */
rkvc_err rkvc_stream_open(rkvc_stream **out,
                          const rkvc_stream_config *cfg);

/**
 * @brief 向流中推入数据。
 *
 * 编码流: 推入原始帧 (rkvc_frame*)。
 * 解码流: 推入压缩数据包 (const rkvc_packet*)。
 *
 * 非阻塞: 缓冲满时返回 RKVC_ERR_AGAIN (除非 drop_frames=1)。
 */
rkvc_err rkvc_stream_push(rkvc_stream *s, const void *data);

/**
 * @brief 从流中拉取结果。
 *
 * 编码流: 拉取编码包 (rkvc_packet*)。
 * 解码流: 拉取解码帧 (rkvc_frame*)。
 *
 * 非阻塞: 无数据时返回 RKVC_ERR_AGAIN。
 * @param timeout_ms  超时毫秒 (-1 表示阻塞等待)。
 */
rkvc_err rkvc_stream_pull(rkvc_stream *s, void *out, int timeout_ms);

/**
 * @brief 通知流不再有新输入（触发 flush）。
 */
rkvc_err rkvc_stream_finish(rkvc_stream *s);

/**
 * @brief 关闭流并释放所有资源。
 */
rkvc_err rkvc_stream_close(rkvc_stream *s);

/**
 * @brief 获取流的统计信息。
 */
typedef struct {
    uint64_t frames_in;       /**< 已输入帧数 */
    uint64_t frames_out;      /**< 已输出帧数 */
    uint64_t frames_dropped;  /**< 丢弃帧数 */
    double   avg_fps;         /**< 平均输出帧率 */
    double   avg_latency_ms;  /**< 平均处理延迟 (ms) */
} rkvc_stream_stats;

rkvc_err rkvc_stream_get_stats(const rkvc_stream *s,
                               rkvc_stream_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* RKVC_STREAM_H */
