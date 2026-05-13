/**
 * @file decoder.h
 * @brief H.265 (HEVC) RKMPP 硬件解码器 API。
 *
 * 支持两种使用模式:
 *  - 离线模式: 打开解码器时指定输入文件路径，按帧读取解码后的视频帧。
 *  - 回调模式: 手动送入压缩数据包，取出解码帧。
 */

#ifndef RKVC_DECODER_H
#define RKVC_DECODER_H

#include "rkvc/types.h"
#include "rkvc/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 解码器配置 ───────────────────────────────────────────────────── */

typedef struct {
    rkvc_pix_fmt   output_format;  /**< 期望输出像素格式 (默认 NV12) */
    int            threads;        /**< 线程数 (默认 0=自动) */
    int            low_delay;      /**< 低延迟模式 (默认 0) */
} rkvc_decoder_config;

/**
 * @brief 返回默认解码配置。
 *
 * NV12 输出，自动线程，非低延迟。
 */
rkvc_decoder_config rkvc_decoder_config_defaults(void);

/* ── 解码器生命周期 ──────────────────────────────────────────────── */

/** 不透明解码器句柄 */
typedef struct rkvc_decoder rkvc_decoder;

/**
 * @brief 打开解码器（无文件输入模式）。
 *
 * 通过 rkvc_decoder_send_packet() 送入压缩数据。
 */
rkvc_err rkvc_decoder_open(rkvc_decoder **out,
                           const rkvc_decoder_config *cfg);

/**
 * @brief 打开解码器并从文件读取。
 *
 * 内部自动创建 demuxer。根据文件内容自动探测流信息。
 *
 * @param cfg         解码配置
 * @param input_path  输入文件路径
 */
rkvc_err rkvc_decoder_open_file(rkvc_decoder **out,
                                const rkvc_decoder_config *cfg,
                                const char *input_path);

/**
 * @brief 送入一包压缩数据。
 *
 * @param data  压缩数据指针
 * @param size  数据大小
 * @param pts   显示时间戳
 * @param dts   解码时间戳
 */
rkvc_err rkvc_decoder_send_packet(rkvc_decoder *dec,
                                  const uint8_t *data, int size,
                                  int64_t pts, int64_t dts);

/**
 * @brief 从文件模式读取并送入下一包。
 *
 * @retval RKVC_OK      成功送入一包
 * @retval RKVC_ERR_EOF 文件读取完毕
 */
rkvc_err rkvc_decoder_read_packet(rkvc_decoder *dec);

/**
 * @brief 取出一帧解码后的视频帧。
 *
 * @param f  输出帧句柄。caller 负责最终 unref。
 * @retval RKVC_OK       成功取出一帧
 * @retval RKVC_ERR_AGAIN 需要更多输入数据
 * @retval RKVC_ERR_EOF   解码器已 flush 完毕
 */
rkvc_err rkvc_decoder_receive_frame(rkvc_decoder *dec, rkvc_frame **f);

/**
 * @brief Drain: 通知解码器不再有新数据，flush 所有缓冲帧。
 */
rkvc_err rkvc_decoder_drain(rkvc_decoder *dec);

/**
 * @brief 关闭解码器并释放所有资源。
 */
rkvc_err rkvc_decoder_close(rkvc_decoder *dec);

/**
 * @brief 获取源视频的宽/高/帧率等信息（文件模式有效）。
 */
rkvc_err rkvc_decoder_get_video_info(const rkvc_decoder *dec,
                                     int *width, int *height,
                                     int *fps_num, int *fps_den);

/**
 * @brief 获取源视频时长（微秒，文件模式有效）。
 */
rkvc_err rkvc_decoder_get_duration(const rkvc_decoder *dec,
                                   int64_t *duration_us);

#ifdef __cplusplus
}
#endif

#endif /* RKVC_DECODER_H */
