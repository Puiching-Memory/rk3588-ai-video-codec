/**
 * @file init.c
 * @brief 全局初始化、版本、能力查询。
 */

#include "internal.h"
#include <pthread.h>
#include <sys/stat.h>

#define RKVC_VERSION_STR "0.1.4"
#define RKVC_VERSION_NUM 0x000104

static pthread_once_t s_init_once = PTHREAD_ONCE_INIT;
static int s_initialized = 0;

static void rkvc_init_impl(void)
{
    /* FFmpeg 全局初始化在较新版本中自动完成，此处确保网络子系统 */
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
    avformat_network_init();
#endif
    s_initialized = 1;
    RKVC_LOG("rkvc initialized");
}

rkvc_err rkvc_init(void)
{
    if (pthread_once(&s_init_once, rkvc_init_impl) != 0)
        return RKVC_ERR_INTERNAL;
    return RKVC_OK;
}

void rkvc_deinit(void)
{
    /*
     * 注意: 不调用 avformat_network_deinit()。
     * 多个编解码器可能共享全局 FFmpeg 状态，
     * 强制反初始化会导致后续实例崩溃。
     */
    s_initialized = 0;
}

const char *rkvc_version(void)
{
    return RKVC_VERSION_STR;
}

uint32_t rkvc_version_number(void)
{
    return RKVC_VERSION_NUM;
}

const char *rkvc_err_str(rkvc_err err)
{
    switch (err) {
    case RKVC_OK:            return "success";
    case RKVC_ERR_NOMEM:     return "out of memory";
    case RKVC_ERR_INVALID:   return "invalid parameter";
    case RKVC_ERR_NOT_FOUND: return "codec or device not found";
    case RKVC_ERR_IO:        return "I/O error";
    case RKVC_ERR_HW:        return "hardware acceleration failed";
    case RKVC_ERR_EOF:       return "end of stream";
    case RKVC_ERR_AGAIN:     return "need more input / output full";
    case RKVC_ERR_MUX:       return "muxer error";
    case RKVC_ERR_INTERNAL:  return "internal error";
    }
    return "unknown error";
}

/* ── 能力查询 ──────────────────────────────────────────────────────── */

rkvc_err rkvc_query_caps(rkvc_caps *caps)
{
    if (!caps)
        return RKVC_ERR_INVALID;

    memset(caps, 0, sizeof(*caps));

    /* 检查 RKMPP 编码器 */
    const AVCodec *enc = avcodec_find_encoder_by_name("hevc_rkmpp");
    caps->has_rkmpp_enc = (enc != NULL);

    /* 检查 RKMPP 解码器 */
    const AVCodec *dec = avcodec_find_decoder_by_name("hevc_rkmpp");
    caps->has_rkmpp_dec = (dec != NULL);

    /* 检查 /dev/dma_heap */
    struct stat st;
    caps->has_dma_heap = (stat("/dev/dma_heap", &st) == 0);

    /* 检查 /dev/rga */
    caps->has_rga = (stat("/dev/rga", &st) == 0);

    /* RK3588 最大分辨率 */
    caps->max_width  = 7680;
    caps->max_height = 4320;

    return RKVC_OK;
}
