/**
 * @file rkvc.h
 * @brief RK3588 Video Codec Library — 公共 API 入口
 *
 * 基于 ffmpeg-rockchip 的 RKMPP 硬件加速 H.265 编解码 C 库，
 * 支持离线文件处理和实时流式处理。
 *
 * 典型用法（编码）:
 * @code
 *   rkvc_encoder *enc = NULL;
 *   rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
 *   cfg.width = 1920; cfg.height = 1080;
 *   cfg.fps_num = 30; cfg.fps_den = 1;
 *   cfg.bitrate  = 4000000;
 *
 *   rkvc_err err = rkvc_encoder_open_file(&enc, &cfg, "output.h265");
 *   if (err != RKVC_OK) { ... }
 *
 *   // 送入原始帧 (NV12)
 *   rkvc_frame *frame = NULL;
 *   rkvc_frame_alloc(&frame, cfg.width, cfg.height, cfg.input_format);
 *   // 填充 frame 像素数据 ...
 *   err = rkvc_encoder_send_frame(enc, frame);
 *   rkvc_frame_unref(frame);
 *
 *   // 文件模式下 receive_packet 会自动写入 output.h265
 *   rkvc_packet pkt;
 *   while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
 *   }
 *
 *   rkvc_encoder_close(enc);
 * @endcode
 *
 * 典型用法（流式）:
 * @code
 *   rkvc_stream *stream = NULL;
 *   rkvc_stream_config scfg = rkvc_stream_config_defaults();
 *   scfg.direction = RKVC_STREAM_ENCODE;
 *   scfg.width = 1920; scfg.height = 1080;
 *
 *   rkvc_stream_open(&stream, &scfg);
 *   rkvc_stream_push(stream, frame);
 *
 *   rkvc_packet pkt;
 *   while (rkvc_stream_pull(stream, &pkt, 0) == RKVC_OK) {
 *       // 处理 pkt.data, pkt.size
 *   }
 *   rkvc_stream_close(stream);
 * @endcode
 */

#ifndef RKVC_H
#define RKVC_H

#include <stddef.h>
#include <stdint.h>

#include "rkvc/types.h"
#include "rkvc/frame.h"
#include "rkvc/scale.h"
#include "rkvc/encoder.h"
#include "rkvc/decoder.h"
#include "rkvc/stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取库版本字符串。
 */
const char *rkvc_version(void);

/**
 * @brief 获取库版本号 (major << 16 | minor << 8 | patch)。
 */
uint32_t rkvc_version_number(void);

/**
 * @brief 初始化全局资源（线程安全，可多次调用）。
 *
 * 内部初始化 FFmpeg 网络、锁等全局子系统。
 * 也可由各子模块自动懒初始化。
 */
rkvc_err rkvc_init(void);

/**
 * @brief 释放全局资源。
 */
void rkvc_deinit(void);

/**
 * @brief 将错误码转换为可读字符串。
 */
const char *rkvc_err_str(rkvc_err err);

/**
 * @brief 运行时能力查询。
 */
typedef struct {
    int has_rkmpp_enc;   /**< RKMPP 编码器可用 */
    int has_rkmpp_dec;   /**< RKMPP 解码器可用 */
    int has_dma_heap;    /**< /dev/dma_heap 可访问 */
    int has_rga;         /**< RGA 2D 加速可用 */
    int max_width;       /**< 硬件支持的最大宽度 */
    int max_height;      /**< 硬件支持的最大高度 */
} rkvc_caps;

/**
 * @brief 查询运行时硬件能力。
 */
rkvc_err rkvc_query_caps(rkvc_caps *caps);

#ifdef __cplusplus
}
#endif

#endif /* RKVC_H */
