/**
 * @file rkvc.h
 * @brief RK3588 Video Codec Library v2 — Session / Pipeline API
 */

#ifndef RKVC_H
#define RKVC_H

#include <stddef.h>
#include <stdint.h>

#include "rkvc/types.h"
#include "rkvc/buffer.h"
#include "rkvc/pipeline.h"
#include "rkvc/policy.h"
#include "rkvc/port.h"
#include "rkvc/session.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *rkvc_version(void);
uint32_t rkvc_version_number(void);

rkvc_err rkvc_init(void);
void rkvc_deinit(void);
const char *rkvc_err_str(rkvc_err err);

typedef enum {
    RKVC_INPUT_UNKNOWN = 0,
    RKVC_INPUT_RAW_VIDEO,
    RKVC_INPUT_COMPRESSED_VIDEO,
} rkvc_input_format_probe;

rkvc_input_format_probe rkvc_probe_input_format(const uint8_t *data,
                                                size_t size);

typedef struct {
    int has_h264_enc;
    int has_hevc_enc;
    int has_av1_enc;      /**< SVT-AV1 */
    int has_h264_dec;
    int has_hevc_dec;
    int has_av1_dec;
    int has_dma_heap;
    int has_rga;
    int max_width;
    int max_height;
} rkvc_caps;

rkvc_err rkvc_query_caps(rkvc_caps *caps);
rkvc_err rkvc_check_hw_permissions(void);

int rkvc_upscale_algo_from_name(const char *name, rkvc_upscale_algo *out);
const char *rkvc_upscale_algo_name(rkvc_upscale_algo algo);

/** YUV420p 平面缓冲上采样（RGA 硬件）。 */
rkvc_err rkvc_upscale_yuv420p(const uint8_t *src, uint8_t *dst,
                              int src_w, int src_h,
                              int dst_w, int dst_h,
                              rkvc_upscale_algo algo);

#ifdef __cplusplus
}
#endif

#endif /* RKVC_H */
