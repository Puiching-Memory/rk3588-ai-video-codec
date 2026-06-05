/**
 * @file encoder.h
 * @brief H.265 (HEVC) RKMPP 硬件编码器 API。
 *
 * 支持两种使用模式:
 *  - 离线模式: 打开编码器时指定输出文件路径，close 时自动 flush + 关闭 muxer。
 *  - 回调模式: 不指定输出文件，通过 receive_packet 回调获取编码数据。
 */

#ifndef RKVC_ENCODER_H
#define RKVC_ENCODER_H

#include "rkvc/types.h"
#include "rkvc/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 编码器配置 ───────────────────────────────────────────────────── */

typedef struct {
    int            width;           /**< 视频宽度 (必填) */
    int            height;          /**< 视频高度 (必填) */
    int            fps_num;         /**< 帧率分子 (默认 30) */
    int            fps_den;         /**< 帧率分母 (默认 1) */
    int64_t        bitrate;         /**< 目标码率 bps (默认 4 Mbps) */
    int            gop_size;        /**< GOP 大小 (默认 60) */
    rkvc_pix_fmt   input_format;    /**< 输入像素格式 (默认 NV12) */
    rkvc_preset    preset;          /**< 编码预设 (默认 MEDIUM) */
    rkvc_rc_mode   rc_mode;         /**< 码率控制 (默认 CBR) */
    int            qp;              /**< CQP 模式下的 QP 值 (默认 26) */
    int            profile;         /**< HEVC profile (0=自动) */
    int            level;           /**< HEVC level (0=自动) */
    int            num_b_frames;    /**< B帧数量 (默认 0, RKMPP 推荐) */
    int            threads;         /**< 线程数 (默认 0=自动) */
} rkvc_encoder_config;

/**
 * @brief 返回默认编码配置。
 *
 * 1920x1080, 30fps, 4Mbps CBR, NV12, MEDIUM preset。
 */
rkvc_encoder_config rkvc_encoder_config_defaults(void);

/* ── 编码包 ───────────────────────────────────────────────────────── */

typedef struct {
    const uint8_t *data;     /**< 编码数据指针 (编码器内部缓冲区, 仅在 receive 后有效) */
    int            size;     /**< 数据大小 */
    int64_t        pts;      /**< 显示时间戳 */
    int64_t        dts;      /**< 解码时间戳 */
    int            key_frame;/**< 是否为关键帧 */
    int64_t        pos;      /**< 字节位置 (文件模式) */
} rkvc_packet;

/* ── 编码器生命周期 ──────────────────────────────────────────────── */

/** 不透明编码器句柄 */
typedef struct rkvc_encoder rkvc_encoder;

/**
 * @brief 打开编码器（无文件输出模式）。
 *
 * 通过 rkvc_encoder_receive_packet() 获取编码后的数据包。
 */
rkvc_err rkvc_encoder_open(rkvc_encoder **out,
                           const rkvc_encoder_config *cfg);

/**
 * @brief 打开编码器并直接输出到文件。
 *
 * 内部自动创建 HEVC muxer。close 时自动写入 trailer。
 *
 * @param cfg         编码配置
 * @param output_path 输出文件路径 (.h265 / .mp4 / .mkv 等, 根据扩展名自动选择 muxer)
 */
rkvc_err rkvc_encoder_open_file(rkvc_encoder **out,
                                const rkvc_encoder_config *cfg,
                                const char *output_path);

/**
 * @brief 送入一帧原始数据。
 *
 * @param f  帧句柄 (NV12 或配置指定格式)。编码器会在需要时引用输入帧；
 *           调用方仍需释放自己的 rkvc_frame 引用。
 * @retval RKVC_ERR_FORMAT  输入看起来是 H.264/H.265 或常见容器，而不是原始像素帧。
 *
 * 送入 NULL 表示 flush (等价于后面调用 rkvc_encoder_drain)。
 */
rkvc_err rkvc_encoder_send_frame(rkvc_encoder *enc, rkvc_frame *f);

/**
 * @brief 送入原始像素缓冲区（零拷贝快捷接口）。
 *
 * 适用于不想分配 rkvc_frame 的场景。内部自动创建临时帧。
 * @retval RKVC_ERR_FORMAT  输入看起来是 H.264/H.265 或常见容器，而不是原始像素缓冲区。
 */
rkvc_err rkvc_encoder_send_buffer(rkvc_encoder *enc,
                                  const uint8_t *data, int linesize,
                                  int64_t pts);

/**
 * @brief 取出一个编码包。
 *
 * @param pkt  输出编码包。data 指针在下次 send/receive/close 前有效。
 * @retval RKVC_OK     成功取出一个包
 * @retval RKVC_ERR_AGAIN  需要更多输入帧
 * @retval RKVC_ERR_EOF    编码器已 flush 完毕
 */
rkvc_err rkvc_encoder_receive_packet(rkvc_encoder *enc, rkvc_packet *pkt);

/**
 * @brief Drain: 通知编码器不再有新帧，flush 所有缓冲的包。
 *
 * 之后 receive_packet 逐个返回剩余包，直到返回 RKVC_ERR_EOF。
 */
rkvc_err rkvc_encoder_drain(rkvc_encoder *enc);

/**
 * @brief 关闭编码器并释放所有资源。
 *
 * 如果是文件模式，自动写入文件 trailer。
 * 如果未 drain，内部自动 drain。
 */
rkvc_err rkvc_encoder_close(rkvc_encoder *enc);

/**
 * @brief 获取编码器内部时基。
 */
rkvc_err rkvc_encoder_timebase(const rkvc_encoder *enc,
                               int *num, int *den);

/**
 * @brief 获取编码器配置（只读）。
 */
rkvc_err rkvc_encoder_get_config(const rkvc_encoder *enc,
                                 rkvc_encoder_config *cfg);

#ifdef __cplusplus
}
#endif

#endif /* RKVC_ENCODER_H */
