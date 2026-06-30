/**
 * @file session.h
 * @brief rkvc v2 会话：图式编解码管线。
 */

#ifndef RKVC_SESSION_H
#define RKVC_SESSION_H

#include "rkvc/pipeline.h"
#include "rkvc/port.h"
#include "rkvc/policy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rkvc_session rkvc_session;

typedef struct {
    rkvc_route_plan route;
    int             running;
    uint64_t        frames_in;
    uint64_t        frames_out;
    uint64_t        frames_dropped;
    double          avg_fps;
} rkvc_session_stats;

rkvc_err rkvc_session_create(const rkvc_pipeline_desc *desc,
                             rkvc_session **out);
rkvc_err rkvc_session_start(rkvc_session *session);
rkvc_err rkvc_session_stop(rkvc_session *session);
rkvc_err rkvc_session_get_route(const rkvc_session *session,
                                rkvc_route_plan *plan);
rkvc_port *rkvc_session_port(rkvc_session *session, const char *name);
rkvc_err rkvc_session_get_stats(const rkvc_session *session,
                                rkvc_session_stats *stats);
void rkvc_session_destroy(rkvc_session *session);

/** 文件模板：阻塞跑完整条管线（encode/decode/transcode） */
rkvc_err rkvc_session_run_file(rkvc_session *session);

#ifdef __cplusplus
}
#endif

#endif /* RKVC_SESSION_H */
