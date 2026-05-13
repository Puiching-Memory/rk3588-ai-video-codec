/**
 * @file frame.h
 * @brief 视频帧管理。
 *
 * rkvc_frame 是一个不透明的视频帧句柄，内部持有 FFmpeg AVFrame
 * 及 RKMPP 硬件表面，对外只暴露只读元数据。
 */

#ifndef RKVC_FRAME_H
#define RKVC_FRAME_H

#include "rkvc/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 帧元数据（只读） ─────────────────────────────────────────────── */

typedef struct {
    int            width;
    int            height;
    rkvc_pix_fmt   format;
    int64_t        pts;        /**< presentation timestamp (时基单位) */
    int            key_frame;
} rkvc_frame_info;

/* ── 帧生命周期 ──────────────────────────────────────────────────── */

/** 不透明帧句柄 */
typedef struct rkvc_frame rkvc_frame;

/**
 * @brief 分配一个空帧（软件缓冲区）。
 *
 * 用于向编码器送入原始像素数据。caller 负责填充像素。
 * @param width   帧宽度
 * @param height  帧高度
 * @param format  像素格式
 */
rkvc_err rkvc_frame_alloc(rkvc_frame **out, int width, int height,
                          rkvc_pix_fmt format);

/**
 * @brief 获取帧的只读元数据。
 */
rkvc_err rkvc_frame_get_info(const rkvc_frame *f, rkvc_frame_info *info);

/**
 * @brief 获取帧各平面数据指针及行跨度。
 *
 * @param f        帧句柄
 * @param planes   输出: 各平面数据指针数组 (至少 4 个元素)
 * @param strides  输出: 各平面对应行跨度数组 (至少 4 个元素)
 */
rkvc_err rkvc_frame_get_data(const rkvc_frame *f,
                             uint8_t *planes[4], int strides[4]);

/**
 * @brief 设置帧的 PTS。
 */
rkvc_err rkvc_frame_set_pts(rkvc_frame *f, int64_t pts);

/**
 * @brief 引用计数 +1。
 */
rkvc_frame *rkvc_frame_ref(rkvc_frame *f);

/**
 * @brief 引用计数 -1，归零时释放。
 */
void rkvc_frame_unref(rkvc_frame *f);

#ifdef __cplusplus
}
#endif

#endif /* RKVC_FRAME_H */
