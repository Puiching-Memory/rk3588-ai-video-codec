/**
 * @file session.c
 * @brief rkvc v2 会话：图构建与文件/端口管线。
 */

#include "internal.h"

#include <sys/time.h>

static int session_align2(int v)
{
    return v > 0 ? (v & ~1) : 0;
}

static void session_enc_size(const rkvc_session *s, int *ew, int *eh)
{
    int denom = s->desc.enc_scale_denom;
    if (denom <= 1) {
        *ew = s->desc.width;
        *eh = s->desc.height;
        return;
    }
    *ew = session_align2(s->desc.width / denom);
    *eh = session_align2(s->desc.height / denom);
}

static int session_needs_post_upscale(const rkvc_session *s)
{
    return s->desc.post_upscale_algo != RKVC_UPSCALE_NONE &&
           s->desc.enc_scale_denom > 1;
}

static rkvc_err session_downscale_for_encode(rkvc_session *s,
                                             rkvc_buffer *frame,
                                             rkvc_buffer **out)
{
    int ew = 0, eh = 0;
    session_enc_size(s, &ew, &eh);
    *out = frame;

    if (s->desc.enc_scale_denom <= 1)
        return RKVC_OK;

    if ((int)frame->width == ew && (int)frame->height == eh)
        return RKVC_OK;

    rkvc_buffer *scaled = NULL;
    rkvc_err err = rkvc_rga_scale_buffer(frame, &scaled, ew, eh,
                                         s->desc.pixel_format,
                                         RKVC_UPSCALE_BILINEAR);
    if (err != RKVC_OK)
        return err;
    scaled->pts = frame->pts;
    *out = scaled;
    return RKVC_OK;
}

static rkvc_err session_apply_post_upscale(rkvc_session *s,
                                           rkvc_buffer *host,
                                           rkvc_buffer **out)
{
    *out = host;
    if (!session_needs_post_upscale(s))
        return RKVC_OK;

    rkvc_buffer *up = NULL;
    rkvc_err err = rkvc_post_upscale_buffer(host, &up,
                                            s->desc.width, s->desc.height,
                                            s->desc.post_upscale_algo);
    if (err != RKVC_OK)
        return err;
    up->pts = host->pts;
    *out = up;
    return RKVC_OK;
}

static void port_init(rkvc_port *p, const char *name, int depth,
                      rkvc_session *s)
{
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", name);
    p->queue   = rkvc_port_queue_create(depth);
    p->session = s;
}

static rkvc_err session_open_nodes(rkvc_session *s)
{
    const rkvc_pipeline_desc *d = &s->desc;

    if (d->template_id == RKVC_TEMPLATE_FILE_TRANSCODE ||
        d->template_id == RKVC_TEMPLATE_FILE_DECODE ||
        d->template_id == RKVC_TEMPLATE_AV1_STORAGE) {
        if (!d->input_path)
            return RKVC_ERR_INVALID;

        rkvc_demux_config dc = { .input_path = d->input_path };
        rkvc_err err = rkvc_demux_open(&s->demux, &dc);
        if (err != RKVC_OK)
            return err;

        AVCodecParameters *par = rkvc_demux_video_par(s->demux);
        rkvc_mpp_dec_config mdc = {
            .route         = &s->route,
            .output_format = d->pixel_format,
            .low_latency   = d->low_latency,
        };
        err = rkvc_mpp_dec_open(&s->dec, &mdc, par);
        if (err != RKVC_OK)
            return err;
    }

    if (d->template_id == RKVC_TEMPLATE_FILE_ENCODE ||
        d->template_id == RKVC_TEMPLATE_LIVE_CAPTURE) {
        if (!d->output_path)
            return RKVC_ERR_INVALID;
    }

    if (d->template_id == RKVC_TEMPLATE_FILE_TRANSCODE ||
        d->template_id == RKVC_TEMPLATE_FILE_ENCODE ||
        d->template_id == RKVC_TEMPLATE_LIVE_CAPTURE ||
        d->template_id == RKVC_TEMPLATE_AV1_STORAGE) {
        if (!d->output_path)
            return RKVC_ERR_INVALID;

        if (s->route.enc_backend == RKVC_ENC_BACKEND_MPP) {
            int ew = 0, eh = 0;
            session_enc_size(s, &ew, &eh);
            rkvc_mpp_enc_config ec = {
                .route        = &s->route,
                .width        = ew,
                .height       = eh,
                .fps_num      = d->fps_num,
                .fps_den      = d->fps_den,
                .bitrate      = d->bitrate,
                .input_format = d->pixel_format,
                .gop_size     = d->gop_size,
                .low_latency  = d->low_latency,
                .rc_mode      = d->rc_mode,
                .qp_init      = d->qp_init,
            };
            rkvc_err err = rkvc_mpp_enc_open(&s->enc, &ec);
            if (err != RKVC_OK)
                return err;
        } else {
            int ew = 0, eh = 0;
            session_enc_size(s, &ew, &eh);
            rkvc_svt_enc_config sc = {
                .width        = ew,
                .height       = eh,
                .fps_num      = d->fps_num,
                .fps_den      = d->fps_den,
                .bitrate      = d->bitrate,
                .input_format = d->pixel_format,
                .gop_size     = d->gop_size,
                .svt_preset   = s->route.svt_preset,
                .rc_mode      = d->rc_mode,
            };
            rkvc_err err = rkvc_svt_enc_open(&s->svt, &sc);
            if (err != RKVC_OK)
                return err;
        }

        int ew = 0, eh = 0;
        session_enc_size(s, &ew, &eh);

        rkvc_mux_config mc = {
            .output_path  = d->output_path,
            .route        = &s->route,
            .width        = ew,
            .height       = eh,
            .fps_num      = d->fps_num,
            .fps_den      = d->fps_den,
            .bitrate      = d->bitrate,
            .pixel_format = d->pixel_format,
        };

        AVCodecParameters *par = avcodec_parameters_alloc();
        if (!par)
            return RKVC_ERR_NOMEM;
        par->codec_type = AVMEDIA_TYPE_VIDEO;
        par->width  = ew;
        par->height = eh;
        par->bit_rate = d->bitrate;
        if (s->route.codec == RKVC_CODEC_HEVC)
            par->codec_id = AV_CODEC_ID_HEVC;
        else if (s->route.codec == RKVC_CODEC_AV1)
            par->codec_id = AV_CODEC_ID_AV1;
        else
            par->codec_id = AV_CODEC_ID_H264;

        if (s->svt)
            rkvc_svt_enc_write_header(s->svt, par);

        rkvc_err err = rkvc_mux_open(&s->mux, &mc, par);
        avcodec_parameters_free(&par);
        if (err != RKVC_OK)
            return err;
    }

    return RKVC_OK;
}

rkvc_err rkvc_session_create(const rkvc_pipeline_desc *desc,
                             rkvc_session **out)
{
    if (!desc || !out)
        return RKVC_ERR_INVALID;

    *out = NULL;
    rkvc_init();

    rkvc_session *s = rkvc_calloc(1, sizeof(*s));
    if (!s)
        return RKVC_ERR_NOMEM;

    s->desc = *desc;
    if (s->desc.queue_depth <= 0)
        s->desc.queue_depth = RKVC_PORT_QUEUE_DEFAULT;

    pthread_mutex_init(&s->lock, NULL);

    rkvc_err err = rkvc_route_resolve(&s->desc, &s->route);
    if (err != RKVC_OK) {
        rkvc_session_destroy(s);
        return err;
    }

    s->pool = rkvc_buffer_pool_create();
    if (!s->pool) {
        rkvc_session_destroy(s);
        return RKVC_ERR_NOMEM;
    }

    port_init(&s->port_capture, "capture", s->desc.queue_depth, s);
    port_init(&s->port_output, "output", s->desc.queue_depth, s);
    port_init(&s->port_preview, "preview", s->desc.queue_depth, s);

    s->stats.route = s->route;
    *out = s;
    return RKVC_OK;
}

rkvc_err rkvc_session_start(rkvc_session *session)
{
    if (!session)
        return RKVC_ERR_INVALID;
    if (session->running)
        return RKVC_OK;

    rkvc_err err = session_open_nodes(session);
    if (err != RKVC_OK)
        return err;

    session->running = 1;
    session->stats.running = 1;
    return RKVC_OK;
}

rkvc_err rkvc_session_stop(rkvc_session *session)
{
    if (!session)
        return RKVC_ERR_INVALID;
    session->stop_requested = 1;
    session->running = 0;
    session->stats.running = 0;
    return RKVC_OK;
}

rkvc_err rkvc_session_get_route(const rkvc_session *session,
                                rkvc_route_plan *plan)
{
    if (!session || !plan)
        return RKVC_ERR_INVALID;
    *plan = session->route;
    return RKVC_OK;
}

rkvc_port *rkvc_session_port(rkvc_session *session, const char *name)
{
    if (!session || !name)
        return NULL;
    if (strcmp(name, "capture") == 0)
        return &session->port_capture;
    if (strcmp(name, "output") == 0)
        return &session->port_output;
    if (strcmp(name, "preview") == 0)
        return &session->port_preview;
    return NULL;
}

rkvc_err rkvc_session_get_stats(const rkvc_session *session,
                                rkvc_session_stats *stats)
{
    if (!session || !stats)
        return RKVC_ERR_INVALID;

    pthread_mutex_lock((pthread_mutex_t *)&session->lock);
    *stats = session->stats;
    pthread_mutex_unlock((pthread_mutex_t *)&session->lock);
    return RKVC_OK;
}

void rkvc_session_destroy(rkvc_session *session)
{
    if (!session)
        return;

    rkvc_session_stop(session);
    if (session->mux)
        rkvc_mux_close(session->mux);
    if (session->enc)
        rkvc_mpp_enc_close(session->enc);
    if (session->svt)
        rkvc_svt_enc_close(session->svt);
    if (session->dec)
        rkvc_mpp_dec_close(session->dec);
    if (session->demux)
        rkvc_demux_close(session->demux);

    rkvc_port_queue_destroy(session->port_capture.queue);
    rkvc_port_queue_destroy(session->port_output.queue);
    rkvc_port_queue_destroy(session->port_preview.queue);

    rkvc_buffer_pool_destroy(session->pool);
    pthread_mutex_destroy(&session->lock);
    rkvc_free(session);
}

static rkvc_err drain_encoder_packets(rkvc_session *s)
{
    for (;;) {
        rkvc_buffer *pkt = NULL;
        rkvc_err err;
        if (s->enc)
            err = rkvc_mpp_enc_receive_packet(s->enc, &pkt);
        else
            err = rkvc_svt_enc_receive_packet(s->svt, &pkt);

        if (err == RKVC_ERR_AGAIN)
            return RKVC_OK;
        if (err == RKVC_ERR_EOF)
            return RKVC_OK;
        if (err != RKVC_OK)
            return err;

        err = rkvc_mux_write_packet(s->mux, pkt);
        rkvc_port_push(&s->port_output, pkt);
        rkvc_session_stats_tick(s, 1);
        rkvc_buffer_unref(pkt);
        if (err != RKVC_OK)
            return err;
    }
}

static rkvc_err encode_one_frame(rkvc_session *s, rkvc_buffer *frame)
{
    if (s->enc) {
        rkvc_err err = rkvc_mpp_enc_send_frame(s->enc, frame);
        if (err != RKVC_OK && err != RKVC_ERR_AGAIN)
            return err;
        return drain_encoder_packets(s);
    }

    for (;;) {
        rkvc_err err = rkvc_svt_enc_send_frame(s->svt, frame);
        if (err == RKVC_OK)
            return drain_encoder_packets(s);
        if (err == RKVC_ERR_AGAIN) {
            err = drain_encoder_packets(s);
            if (err != RKVC_OK)
                return err;
            continue;
        }
        return err;
    }
}

static rkvc_err transcode_loop(rkvc_session *s)
{
    rkvc_err err = RKVC_OK;
    int dec_eof = 0;

    while (!s->stop_requested) {
        if (!dec_eof) {
            rkvc_buffer *pkt = NULL;
            err = rkvc_demux_read_packet(s->demux, &pkt);
            if (err == RKVC_ERR_EOF) {
                dec_eof = 1;
                rkvc_mpp_dec_drain(s->dec);
            } else if (err != RKVC_OK) {
                return err;
            } else {
                err = rkvc_mpp_dec_send_packet(s->dec, pkt);
                rkvc_buffer_unref(pkt);
                if (err != RKVC_OK && err != RKVC_ERR_AGAIN)
                    return err;
            }
        }

        rkvc_buffer *frame = NULL;
        err = rkvc_mpp_dec_receive_frame(s->dec, &frame);
        if (err == RKVC_ERR_AGAIN) {
            if (s->svt)
                drain_encoder_packets(s);
            continue;
        }
        if (err == RKVC_ERR_EOF)
            break;
        if (err != RKVC_OK)
            return err;

        rkvc_session_stats_frame_in(s);

        rkvc_buffer *enc_frame = NULL;
        err = session_downscale_for_encode(s, frame, &enc_frame);
        if (err != RKVC_OK) {
            rkvc_buffer_unref(frame);
            return err;
        }

        err = encode_one_frame(s, enc_frame);
        if (enc_frame != frame)
            rkvc_buffer_unref(enc_frame);
        rkvc_buffer_unref(frame);
        if (err != RKVC_OK)
            return err;
    }

    if (s->enc)
        rkvc_mpp_enc_drain(s->enc);
    else
        rkvc_svt_enc_drain(s->svt);

    for (;;) {
        rkvc_buffer *pkt = NULL;
        if (s->enc)
            err = rkvc_mpp_enc_receive_packet(s->enc, &pkt);
        else
            err = rkvc_svt_enc_receive_packet(s->svt, &pkt);
        if (err == RKVC_ERR_EOF)
            break;
        if (err == RKVC_ERR_AGAIN)
            continue;
        if (err != RKVC_OK)
            return err;
        rkvc_mux_write_packet(s->mux, pkt);
        rkvc_port_push(&s->port_output, pkt);
        rkvc_session_stats_tick(s, 1);
        rkvc_buffer_unref(pkt);
    }

    return RKVC_OK;
}

static rkvc_err decode_loop(rkvc_session *s)
{
    if (!s->desc.output_path)
        return RKVC_ERR_INVALID;

    FILE *fp = fopen(s->desc.output_path, "wb");
    if (!fp)
        return RKVC_ERR_IO;

    rkvc_err err = RKVC_OK;
    int dec_eof = 0;

    while (!s->stop_requested) {
        if (!dec_eof) {
            rkvc_buffer *pkt = NULL;
            err = rkvc_demux_read_packet(s->demux, &pkt);
            if (err == RKVC_ERR_EOF) {
                dec_eof = 1;
                rkvc_mpp_dec_drain(s->dec);
            } else if (err != RKVC_OK) {
                fclose(fp);
                return err;
            } else {
                err = rkvc_mpp_dec_send_packet(s->dec, pkt);
                rkvc_buffer_unref(pkt);
                if (err != RKVC_OK && err != RKVC_ERR_AGAIN) {
                    fclose(fp);
                    return err;
                }
            }
        }

        rkvc_buffer *frame = NULL;
        err = rkvc_mpp_dec_receive_frame(s->dec, &frame);
        if (err == RKVC_ERR_AGAIN) {
            if (dec_eof)
                break;
            continue;
        }
        if (err == RKVC_ERR_EOF)
            break;
        if (err != RKVC_OK) {
            fclose(fp);
            return err;
        }

        rkvc_buffer *host = NULL;
        if (frame->mem_type == RKVC_MEM_DMABUF)
            err = rkvc_dma_to_host(frame, &host);
        else
            host = rkvc_buffer_ref(frame);

        rkvc_buffer *display = NULL;
        if (err == RKVC_OK && host)
            err = session_apply_post_upscale(s, host, &display);

        if (err == RKVC_OK && display && display->av_frame) {
            int h = display->av_frame->height;
            int ls = display->av_frame->linesize[0];
            for (int y = 0; y < h; y++)
                fwrite(display->av_frame->data[0] + y * ls, 1, (size_t)ls, fp);
            for (int y = 0; y < h / 2; y++)
                fwrite(display->av_frame->data[1] + y * display->av_frame->linesize[1],
                       1, (size_t)display->av_frame->linesize[1], fp);
            rkvc_port_push(&s->port_output, display);
            rkvc_session_stats_tick(s, 1);
        }

        if (display != host)
            rkvc_buffer_unref(display);
        rkvc_buffer_unref(host);
        rkvc_buffer_unref(frame);
    }

    fclose(fp);
    return RKVC_OK;
}

static rkvc_err load_raw_frame(FILE *fp, rkvc_buffer *frame, int w, int h,
                               rkvc_pix_fmt fmt)
{
    const size_t y_size = (size_t)w * (size_t)h;
    size_t frame_bytes = y_size + y_size / 2;
    uint8_t *raw = rkvc_malloc(frame_bytes);
    if (!raw)
        return RKVC_ERR_NOMEM;

    if (fread(raw, 1, frame_bytes, fp) != frame_bytes) {
        rkvc_free(raw);
        return RKVC_ERR_EOF;
    }

    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_buffer_get_video_planes(frame, planes, strides);

    for (int y = 0; y < h; y++)
        memcpy(planes[0] + y * strides[0],
               raw + (size_t)y * (size_t)w, (size_t)w);

    if (fmt == RKVC_PIX_FMT_YUV420P) {
        const size_t c_w = (size_t)w / 2;
        const size_t c_h = (size_t)h / 2;
        const uint8_t *u_src = raw + y_size;
        const uint8_t *v_src = u_src + c_w * c_h;
        for (int y = 0; y < (int)c_h; y++) {
            memcpy(planes[1] + y * strides[1], u_src + y * c_w, c_w);
            memcpy(planes[2] + y * strides[2], v_src + y * c_w, c_w);
        }
    } else {
        for (int y = 0; y < h / 2; y++)
            memcpy(planes[1] + y * strides[1],
                   raw + y_size + (size_t)y * (size_t)w, (size_t)w);
    }

    rkvc_free(raw);
    return RKVC_OK;
}

static rkvc_err encode_file_loop(rkvc_session *s)
{
    if (!s->desc.input_path)
        return RKVC_ERR_INVALID;

    FILE *fp = fopen(s->desc.input_path, "rb");
    if (!fp)
        return RKVC_ERR_IO;

    const int w = s->desc.width;
    const int h = s->desc.height;
    rkvc_err err = RKVC_OK;
    rkvc_err loop_err = RKVC_OK;
    int64_t pts = 0;

    while (!s->stop_requested) {
        rkvc_buffer *frame = NULL;
        err = rkvc_buffer_alloc_video_host(&frame, w, h, s->desc.pixel_format);
        if (err != RKVC_OK)
            break;

        err = load_raw_frame(fp, frame, w, h, s->desc.pixel_format);
        if (err == RKVC_ERR_EOF) {
            rkvc_buffer_unref(frame);
            err = RKVC_OK;
            break;
        }
        if (err != RKVC_OK) {
            rkvc_buffer_unref(frame);
            break;
        }

        rkvc_buffer_set_pts(frame, pts++);
        rkvc_session_stats_frame_in(s);

        rkvc_buffer *enc_frame = NULL;
        err = session_downscale_for_encode(s, frame, &enc_frame);
        if (err != RKVC_OK) {
            rkvc_buffer_unref(frame);
            break;
        }

        err = encode_one_frame(s, enc_frame);
        if (enc_frame != frame)
            rkvc_buffer_unref(enc_frame);
        rkvc_buffer_unref(frame);
        if (err != RKVC_OK) {
            loop_err = err;
            break;
        }
    }

    fclose(fp);

    if (loop_err != RKVC_OK)
        return loop_err;

    if (s->enc)
        rkvc_mpp_enc_drain(s->enc);
    else
        rkvc_svt_enc_drain(s->svt);

    for (;;) {
        rkvc_buffer *pkt = NULL;
        if (s->enc)
            err = rkvc_mpp_enc_receive_packet(s->enc, &pkt);
        else
            err = rkvc_svt_enc_receive_packet(s->svt, &pkt);
        if (err == RKVC_ERR_EOF)
            break;
        if (err == RKVC_ERR_AGAIN)
            continue;
        if (err != RKVC_OK)
            return err;
        rkvc_mux_write_packet(s->mux, pkt);
        rkvc_port_push(&s->port_output, pkt);
        rkvc_session_stats_tick(s, 1);
        rkvc_buffer_unref(pkt);
    }

    return RKVC_OK;
}

rkvc_err rkvc_session_run_file(rkvc_session *session)
{
    if (!session)
        return RKVC_ERR_INVALID;

    rkvc_err err = rkvc_session_start(session);
    if (err != RKVC_OK)
        return err;

    switch (session->desc.template_id) {
    case RKVC_TEMPLATE_FILE_TRANSCODE:
    case RKVC_TEMPLATE_AV1_STORAGE:
    case RKVC_TEMPLATE_LIVE_CAPTURE:
        err = transcode_loop(session);
        break;
    case RKVC_TEMPLATE_FILE_DECODE:
        err = decode_loop(session);
        break;
    case RKVC_TEMPLATE_FILE_ENCODE:
        err = encode_file_loop(session);
        break;
    default:
        err = RKVC_ERR_INVALID;
    }

    rkvc_session_stop(session);
    return err;
}
