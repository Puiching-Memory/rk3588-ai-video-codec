/**
 * @file buffer.h
 * @brief rkvc v2 统一缓冲区 (视频 DMA / 主机内存 / 码流)。
 */

#ifndef RKVC_BUFFER_H
#define RKVC_BUFFER_H

#include "rkvc/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RKVC_BUF_NONE = 0,
    RKVC_BUF_VIDEO,
    RKVC_BUF_BITSTREAM,
} rkvc_buffer_kind;

typedef enum {
    RKVC_MEM_HOST = 0,
    RKVC_MEM_DMABUF,
} rkvc_mem_type;

typedef struct rkvc_buffer rkvc_buffer;

typedef struct {
    rkvc_mem_type  mem_type;
    int            fd;
    uint32_t       width;
    uint32_t       height;
    rkvc_pix_fmt   format;
    uint32_t       strides[4];
    int64_t        pts;
    uint64_t       modifier;
} rkvc_buffer_video_info;

typedef struct {
    const uint8_t *data;
    size_t         size;
    int64_t        pts;
    int64_t        dts;
    int            key_frame;
} rkvc_buffer_bitstream_view;

rkvc_buffer *rkvc_buffer_ref(rkvc_buffer *buf);
void          rkvc_buffer_unref(rkvc_buffer *buf);

rkvc_buffer_kind rkvc_buffer_kind_of(const rkvc_buffer *buf);

rkvc_err rkvc_buffer_alloc_video_host(rkvc_buffer **out,
                                      int width, int height,
                                      rkvc_pix_fmt format);
rkvc_err rkvc_buffer_alloc_bitstream(rkvc_buffer **out,
                                     const uint8_t *data, size_t size,
                                     int copy);
rkvc_err rkvc_buffer_get_video_info(const rkvc_buffer *buf,
                                    rkvc_buffer_video_info *info);
rkvc_err rkvc_buffer_get_video_planes(rkvc_buffer *buf,
                                      uint8_t *planes[4],
                                      int strides[4]);
rkvc_err rkvc_buffer_get_bitstream(const rkvc_buffer *buf,
                                   rkvc_buffer_bitstream_view *view);
rkvc_err rkvc_buffer_set_pts(rkvc_buffer *buf, int64_t pts);

#ifdef __cplusplus
}
#endif

#endif /* RKVC_BUFFER_H */
