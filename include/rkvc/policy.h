/**
 * @file policy.h
 * @brief Codec 选择与路由策略 (H.264 / HEVC / AV1)。
 */

#ifndef RKVC_POLICY_H
#define RKVC_POLICY_H

#include "rkvc/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rkvc_pipeline_desc rkvc_pipeline_desc;

typedef enum {
    RKVC_CODEC_H264 = 0,
    RKVC_CODEC_HEVC,
    RKVC_CODEC_AV1,
    RKVC_CODEC_AUTO,
} rkvc_codec;

typedef enum {
    RKVC_POLICY_REALTIME = 0,  /**< H.264 RKMPP，目标 >=30fps */
    RKVC_POLICY_BALANCED,      /**< HEVC RKMPP 或 AV1 快档 */
    RKVC_POLICY_QUALITY,       /**< AV1 SVT p11 快档 + av1_rkmpp */
} rkvc_policy;

typedef enum {
    RKVC_ENC_BACKEND_NONE = 0,
    RKVC_ENC_BACKEND_MPP,   /**< h264_rkmpp / hevc_rkmpp */
    RKVC_ENC_BACKEND_SVT,   /**< SVT-AV1 */
} rkvc_enc_backend;

typedef enum {
    RKVC_DEC_BACKEND_NONE = 0,
    RKVC_DEC_BACKEND_MPP,   /**< *_rkmpp */
} rkvc_dec_backend;

typedef struct {
    rkvc_codec        codec;
    rkvc_enc_backend  enc_backend;
    rkvc_dec_backend  dec_backend;
    const char       *enc_name;   /**< FFmpeg 编码器名或 "svt-av1" */
    const char       *dec_name;
    int               svt_preset; /**< SVT enc_mode (6–11) */
    const char       *reason;
} rkvc_route_plan;

/**
 * @brief 根据 policy / codec / 分辨率 选择编解码路线。
 */
rkvc_err rkvc_route_resolve(const rkvc_pipeline_desc *desc,
                            rkvc_route_plan *plan);

const char *rkvc_codec_name(rkvc_codec codec);
const char *rkvc_policy_name(rkvc_policy policy);

#ifdef __cplusplus
}
#endif

#endif /* RKVC_POLICY_H */
