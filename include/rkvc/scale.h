/**
 * @file scale.h
 * @brief RGA 硬件缩放/格式转换 API。
 *
 * 基于 Rockchip RGA 2D 加速器，支持 NV12/YUV420P 等格式的
 * 硬件缩放、裁剪和格式转换。零 CPU 占用。
 */

#ifndef RKVC_SCALE_H
#define RKVC_SCALE_H

#include "rkvc/types.h"
#include "rkvc/frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 缩放选项。
 */
typedef struct {
    int     dst_width;      /**< 目标宽度 (必填) */
    int     dst_height;     /**< 目标高度 (必填) */
    int     dst_format;     /**< 目标格式 (RKVC_PIX_FMT_*, -1 = 保持源格式) */
} rkvc_scale_config;

/**
 * @brief 对帧进行硬件缩放。
 *
 * 使用 RGA 2D 加速器完成缩放，零 CPU 占用。
 * 输入输出均为 rkvc_frame*，内部自动管理缓冲区。
 *
 * @param src   源帧 (NV12/YUV420P)
 * @param out   输出帧 (caller 负责 unref)
 * @param cfg   缩放配置
 * @return RKVC_OK 成功, RKVC_ERR_HW RGA 不可用, 其他错误码
 *
 * @code
 *   rkvc_frame *scaled = NULL;
 *   rkvc_scale_config cfg = { .dst_width = 1280, .dst_height = 720 };
 *   rkvc_frame_scale(src_frame, &scaled, &cfg);
 *   // 使用 scaled ...
 *   rkvc_frame_unref(scaled);
 * @endcode
 */
rkvc_err rkvc_frame_scale(const rkvc_frame *src, rkvc_frame **out,
                          const rkvc_scale_config *cfg);

/**
 * @brief 检查 RGA 硬件缩放是否可用。
 *
 * @return 1 可用, 0 不可用
 */
int rkvc_scale_available(void);

#ifdef __cplusplus
}
#endif

#endif /* RKVC_SCALE_H */
