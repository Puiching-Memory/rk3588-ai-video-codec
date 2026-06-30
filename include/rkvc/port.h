/**
 * @file port.h
 * @brief Session 命名端口 push/pull。
 */

#ifndef RKVC_PORT_H
#define RKVC_PORT_H

#include "rkvc/types.h"
#include "rkvc/buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rkvc_port rkvc_port;

rkvc_err rkvc_port_push(rkvc_port *port, rkvc_buffer *buf);
rkvc_err rkvc_port_pull(rkvc_port *port, rkvc_buffer **buf, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* RKVC_PORT_H */
