/**
 * @file visual_compare.c
 * @brief SDL2 visual comparison demo: source vs encode+decode preview.
 *
 * Usage:
 *   visual_compare -i input.h265 [-b 4000000] [-n max_frames] [-l] [-f]
 *
 * The demo decodes the input file, sends each NV12 frame through rkvc's HEVC
 * encoder, decodes the compressed packets again, and shows the source frame on
 * the left with the reconstructed frame on the right. The bottom panel renders
 * codec parameters, latency, stability, and PSNR using a tiny built-in font so
 * the example only needs SDL2.
 */

#include "rkvc/rkvc.h"

#include <SDL.h>
#include <ctype.h>
#include <getopt.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WINDOW_DEFAULT_W 1280
#define WINDOW_DEFAULT_H 760
#define WINDOW_MIN_W 900
#define WINDOW_MIN_H 560
#define QUEUE_CAPACITY 128

typedef struct {
    int count;
    double mean;
    double m2;
    double min;
    double max;
    double last;
} metric_stats;

typedef struct {
    double y;
    double u;
    double v;
    double weighted;
} psnr_result;

typedef struct {
    uint8_t *u;
    uint8_t *v;
    int width;
    int height;
} yuv_scratch;

typedef struct {
    const uint8_t *y;
    const uint8_t *u;
    const uint8_t *v;
    int y_stride;
    int u_stride;
    int v_stride;
    int u_step;
    int v_step;
    int width;
    int height;
} yuv420_view;

typedef struct {
    rkvc_frame **frames;
    double *input_ms;
    int *index;
    int cap;
    int head;
    int count;
} frame_queue;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *source_tex;
    SDL_Texture *recon_tex;
    yuv_scratch scratch;
    int video_w;
    int video_h;
} gui_ctx;

typedef struct {
    const char *input;
    int bitrate;
    int max_frames;
    int low_delay;
    int fast;
    int psnr_step;
} app_options;

typedef struct {
    int width;
    int height;
    int fps_num;
    int fps_den;
    int target_bitrate;
    int low_delay;
    int running;
    int done;
    int frames_in;
    int packets;
    int frames_out;
    int compared;
    int dropped;
    int unmatched;
    uint64_t total_bytes;
    double start_ms;
    double last_present_ms;
    double frame_interval_ms;
    metric_stats latency;
    metric_stats psnr_weighted;
    psnr_result last_psnr;
    frame_queue originals;
    gui_ctx gui;
} app_state;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

static void stats_add(metric_stats *s, double value)
{
    s->last = value;
    if (s->count == 0) {
        s->min = value;
        s->max = value;
        s->mean = value;
        s->m2 = 0.0;
        s->count = 1;
        return;
    }

    if (value < s->min)
        s->min = value;
    if (value > s->max)
        s->max = value;

    s->count++;
    double delta = value - s->mean;
    s->mean += delta / s->count;
    s->m2 += delta * (value - s->mean);
}

static double stats_stddev(const metric_stats *s)
{
    if (s->count < 2)
        return 0.0;
    return sqrt(s->m2 / (s->count - 1));
}

static void queue_init(frame_queue *q, int cap)
{
    memset(q, 0, sizeof(*q));
    q->frames = (rkvc_frame **)calloc((size_t)cap, sizeof(*q->frames));
    q->input_ms = (double *)calloc((size_t)cap, sizeof(*q->input_ms));
    q->index = (int *)calloc((size_t)cap, sizeof(*q->index));
    q->cap = (q->frames && q->input_ms && q->index) ? cap : 0;
}

static void queue_clear(frame_queue *q)
{
    if (!q)
        return;

    for (int i = 0; i < q->count; i++) {
        int pos = (q->head + i) % q->cap;
        rkvc_frame_unref(q->frames[pos]);
    }

    free(q->frames);
    free(q->input_ms);
    free(q->index);
    memset(q, 0, sizeof(*q));
}

static int queue_push(frame_queue *q, rkvc_frame *frame, double input_ms,
                      int index)
{
    int dropped = 0;

    if (!q || q->cap <= 0)
        return 1;

    if (q->count >= q->cap) {
        rkvc_frame_unref(q->frames[q->head]);
        q->frames[q->head] = NULL;
        q->head = (q->head + 1) % q->cap;
        q->count--;
        dropped = 1;
    }

    int tail = (q->head + q->count) % q->cap;
    q->frames[tail] = rkvc_frame_ref(frame);
    q->input_ms[tail] = input_ms;
    q->index[tail] = index;
    q->count++;
    return dropped;
}

static int queue_pop(frame_queue *q, rkvc_frame **frame, double *input_ms,
                     int *index)
{
    if (!q || q->count <= 0)
        return 0;

    int pos = q->head;
    if (frame)
        *frame = q->frames[pos];
    if (input_ms)
        *input_ms = q->input_ms[pos];
    if (index)
        *index = q->index[pos];

    q->frames[pos] = NULL;
    q->head = (q->head + 1) % q->cap;
    q->count--;
    return 1;
}

static void scratch_free(yuv_scratch *s)
{
    if (!s)
        return;
    free(s->u);
    free(s->v);
    memset(s, 0, sizeof(*s));
}

static int scratch_ensure(yuv_scratch *s, int width, int height)
{
    int uv_w = width / 2;
    int uv_h = height / 2;
    size_t need = (size_t)uv_w * uv_h;

    if (s->u && s->v && s->width == uv_w && s->height == uv_h)
        return 0;

    scratch_free(s);
    s->u = (uint8_t *)malloc(need);
    s->v = (uint8_t *)malloc(need);
    if (!s->u || !s->v) {
        scratch_free(s);
        return -1;
    }

    s->width = uv_w;
    s->height = uv_h;
    return 0;
}

static int texture_update_nv12(gui_ctx *gui, SDL_Texture *texture,
                               rkvc_frame *frame)
{
    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_info info;

    if (!gui || !texture || !frame)
        return -1;

    if (rkvc_frame_get_info(frame, &info) != RKVC_OK ||
        rkvc_frame_get_data(frame, planes, strides) != RKVC_OK ||
        !planes[0]) {
        return -1;
    }

    if (scratch_ensure(&gui->scratch, info.width, info.height) != 0)
        return -1;

    int uv_w = info.width / 2;
    int uv_h = info.height / 2;

    if (info.format == RKVC_PIX_FMT_NV12) {
        if (!planes[1])
            return -1;

        for (int y = 0; y < uv_h; y++) {
            const uint8_t *src = planes[1] + y * strides[1];
            uint8_t *dst_u = gui->scratch.u + y * uv_w;
            uint8_t *dst_v = gui->scratch.v + y * uv_w;
            for (int x = 0; x < uv_w; x++) {
                dst_u[x] = src[x * 2];
                dst_v[x] = src[x * 2 + 1];
            }
        }
    } else if (info.format == RKVC_PIX_FMT_YUV420P) {
        if (!planes[1] || !planes[2])
            return -1;

        for (int y = 0; y < uv_h; y++) {
            memcpy(gui->scratch.u + y * uv_w, planes[1] + y * strides[1],
                   (size_t)uv_w);
            memcpy(gui->scratch.v + y * uv_w, planes[2] + y * strides[2],
                   (size_t)uv_w);
        }
    } else {
        return -1;
    }

    return SDL_UpdateYUVTexture(texture, NULL,
                                planes[0], strides[0],
                                gui->scratch.u, uv_w,
                                gui->scratch.v, uv_w);
}

static double plane_psnr(const uint8_t *a, int a_stride,
                         const uint8_t *b, int b_stride,
                         int width, int height)
{
    double sse = 0.0;

    for (int y = 0; y < height; y++) {
        const uint8_t *arow = a + y * a_stride;
        const uint8_t *brow = b + y * b_stride;
        for (int x = 0; x < width; x++) {
            double d = (double)arow[x] - (double)brow[x];
            sse += d * d;
        }
    }

    double mse = sse / ((double)width * height);
    if (mse < 1.0e-10)
        return 99.99;
    return 10.0 * log10(255.0 * 255.0 / mse);
}

static int frame_as_yuv420_view(rkvc_frame *frame, yuv420_view *view)
{
    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_info info;

    if (!frame || !view)
        return -1;

    if (rkvc_frame_get_info(frame, &info) != RKVC_OK ||
        rkvc_frame_get_data(frame, planes, strides) != RKVC_OK ||
        !planes[0] || !planes[1]) {
        return -1;
    }

    memset(view, 0, sizeof(*view));
    view->y = planes[0];
    view->y_stride = strides[0];
    view->width = info.width;
    view->height = info.height;

    if (info.format == RKVC_PIX_FMT_NV12) {
        view->u = planes[1];
        view->v = planes[1] + 1;
        view->u_stride = strides[1];
        view->v_stride = strides[1];
        view->u_step = 2;
        view->v_step = 2;
    } else if (info.format == RKVC_PIX_FMT_YUV420P) {
        if (!planes[2])
            return -1;
        view->u = planes[1];
        view->v = planes[2];
        view->u_stride = strides[1];
        view->v_stride = strides[2];
        view->u_step = 1;
        view->v_step = 1;
    } else {
        return -1;
    }

    return 0;
}

static int calc_yuv420_psnr(rkvc_frame *source, rkvc_frame *recon,
                            psnr_result *out)
{
    yuv420_view s;
    yuv420_view r;

    if (!source || !recon || !out)
        return -1;

    if (frame_as_yuv420_view(source, &s) != 0 ||
        frame_as_yuv420_view(recon, &r) != 0 ||
        s.width != r.width || s.height != r.height) {
        return -1;
    }

    out->y = plane_psnr(s.y, s.y_stride, r.y, r.y_stride,
                        s.width, s.height);

    double sse_u = 0.0;
    double sse_v = 0.0;
    int uv_w = s.width / 2;
    int uv_h = s.height / 2;

    for (int y = 0; y < uv_h; y++) {
        const uint8_t *su = s.u + y * s.u_stride;
        const uint8_t *sv = s.v + y * s.v_stride;
        const uint8_t *ru = r.u + y * r.u_stride;
        const uint8_t *rv = r.v + y * r.v_stride;
        for (int x = 0; x < uv_w; x++) {
            double du = (double)su[x * s.u_step] -
                        (double)ru[x * r.u_step];
            double dv = (double)sv[x * s.v_step] -
                        (double)rv[x * r.v_step];
            sse_u += du * du;
            sse_v += dv * dv;
        }
    }

    double denom = (double)uv_w * uv_h;
    double mse_u = sse_u / denom;
    double mse_v = sse_v / denom;
    out->u = (mse_u < 1.0e-10) ? 99.99
            : 10.0 * log10(255.0 * 255.0 / mse_u);
    out->v = (mse_v < 1.0e-10) ? 99.99
            : 10.0 * log10(255.0 * 255.0 / mse_v);
    out->weighted = (6.0 * out->y + 1.5 * out->u + 1.5 * out->v) / 9.0;
    return 0;
}

static const char *glyph_for(char c)
{
    switch ((char)toupper((unsigned char)c)) {
    case 'A': return "01110" "10001" "10001" "11111" "10001" "10001" "10001";
    case 'B': return "11110" "10001" "10001" "11110" "10001" "10001" "11110";
    case 'C': return "01111" "10000" "10000" "10000" "10000" "10000" "01111";
    case 'D': return "11110" "10001" "10001" "10001" "10001" "10001" "11110";
    case 'E': return "11111" "10000" "10000" "11110" "10000" "10000" "11111";
    case 'F': return "11111" "10000" "10000" "11110" "10000" "10000" "10000";
    case 'G': return "01111" "10000" "10000" "10111" "10001" "10001" "01111";
    case 'H': return "10001" "10001" "10001" "11111" "10001" "10001" "10001";
    case 'I': return "01110" "00100" "00100" "00100" "00100" "00100" "01110";
    case 'J': return "00001" "00001" "00001" "00001" "10001" "10001" "01110";
    case 'K': return "10001" "10010" "10100" "11000" "10100" "10010" "10001";
    case 'L': return "10000" "10000" "10000" "10000" "10000" "10000" "11111";
    case 'M': return "10001" "11011" "10101" "10101" "10001" "10001" "10001";
    case 'N': return "10001" "11001" "10101" "10011" "10001" "10001" "10001";
    case 'O': return "01110" "10001" "10001" "10001" "10001" "10001" "01110";
    case 'P': return "11110" "10001" "10001" "11110" "10000" "10000" "10000";
    case 'Q': return "01110" "10001" "10001" "10001" "10101" "10010" "01101";
    case 'R': return "11110" "10001" "10001" "11110" "10100" "10010" "10001";
    case 'S': return "01111" "10000" "10000" "01110" "00001" "00001" "11110";
    case 'T': return "11111" "00100" "00100" "00100" "00100" "00100" "00100";
    case 'U': return "10001" "10001" "10001" "10001" "10001" "10001" "01110";
    case 'V': return "10001" "10001" "10001" "10001" "10001" "01010" "00100";
    case 'W': return "10001" "10001" "10001" "10101" "10101" "10101" "01010";
    case 'X': return "10001" "10001" "01010" "00100" "01010" "10001" "10001";
    case 'Y': return "10001" "10001" "01010" "00100" "00100" "00100" "00100";
    case 'Z': return "11111" "00001" "00010" "00100" "01000" "10000" "11111";
    case '0': return "01110" "10001" "10011" "10101" "11001" "10001" "01110";
    case '1': return "00100" "01100" "00100" "00100" "00100" "00100" "01110";
    case '2': return "01110" "10001" "00001" "00010" "00100" "01000" "11111";
    case '3': return "11110" "00001" "00001" "01110" "00001" "00001" "11110";
    case '4': return "00010" "00110" "01010" "10010" "11111" "00010" "00010";
    case '5': return "11111" "10000" "10000" "11110" "00001" "00001" "11110";
    case '6': return "01110" "10000" "10000" "11110" "10001" "10001" "01110";
    case '7': return "11111" "00001" "00010" "00100" "01000" "01000" "01000";
    case '8': return "01110" "10001" "10001" "01110" "10001" "10001" "01110";
    case '9': return "01110" "10001" "10001" "01111" "00001" "00001" "01110";
    case '.': return "00000" "00000" "00000" "00000" "00000" "00100" "00100";
    case ':': return "00000" "00100" "00100" "00000" "00100" "00100" "00000";
    case '-': return "00000" "00000" "00000" "11111" "00000" "00000" "00000";
    case '+': return "00000" "00100" "00100" "11111" "00100" "00100" "00000";
    case '/': return "00001" "00010" "00010" "00100" "01000" "01000" "10000";
    case '%': return "11001" "11010" "00010" "00100" "01000" "01011" "10011";
    case '@': return "01110" "10001" "10111" "10101" "10111" "10000" "01110";
    case '(': return "00010" "00100" "01000" "01000" "01000" "00100" "00010";
    case ')': return "01000" "00100" "00010" "00010" "00010" "00100" "01000";
    case '_': return "00000" "00000" "00000" "00000" "00000" "00000" "11111";
    case ' ': return "00000" "00000" "00000" "00000" "00000" "00000" "00000";
    default:  return "11111" "00001" "00010" "00100" "00000" "00100" "00000";
    }
}

static void draw_text(SDL_Renderer *renderer, int x, int y, int scale,
                      const char *text, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    int cursor = x;
    for (const char *p = text; *p; p++) {
        const char *glyph = glyph_for(*p);
        for (int gy = 0; gy < 7; gy++) {
            for (int gx = 0; gx < 5; gx++) {
                if (glyph[gy * 5 + gx] == '1') {
                    SDL_Rect px = {
                        cursor + gx * scale,
                        y + gy * scale,
                        scale,
                        scale
                    };
                    SDL_RenderFillRect(renderer, &px);
                }
            }
        }
        cursor += 6 * scale;
    }
}

static void draw_textf(SDL_Renderer *renderer, int x, int y, int scale,
                       SDL_Color color, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    draw_text(renderer, x, y, scale, buf, color);
}

static SDL_Rect fit_rect(int x, int y, int w, int h, int source_w,
                         int source_h)
{
    SDL_Rect out = {x, y, w, h};
    double sx = (double)w / source_w;
    double sy = (double)h / source_h;
    double s = sx < sy ? sx : sy;
    out.w = (int)(source_w * s);
    out.h = (int)(source_h * s);
    if (out.w < 1)
        out.w = 1;
    if (out.h < 1)
        out.h = 1;
    out.x = x + (w - out.w) / 2;
    out.y = y + (h - out.h) / 2;
    return out;
}

static void draw_panel(SDL_Renderer *renderer, SDL_Rect rect,
                       SDL_Color fill, SDL_Color border)
{
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &rect);
}

static void render_state(app_state *state)
{
    SDL_Renderer *renderer = state->gui.renderer;
    int win_w = 0;
    int win_h = 0;
    SDL_GetRendererOutputSize(renderer, &win_w, &win_h);

    SDL_Color bg = {16, 18, 22, 255};
    SDL_Color panel = {28, 32, 38, 255};
    SDL_Color border = {82, 92, 105, 255};
    SDL_Color text = {228, 234, 239, 255};
    SDL_Color dim = {155, 166, 178, 255};
    SDL_Color accent = {74, 188, 154, 255};
    SDL_Color warn = {246, 176, 82, 255};

    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(renderer);

    int margin = 16;
    int gap = 14;
    int bottom_h = 154;
    int title_h = 28;
    int video_y = margin + title_h;
    int video_h = win_h - bottom_h - video_y - margin;
    if (video_h < 120)
        video_h = 120;
    int pane_w = (win_w - margin * 2 - gap) / 2;

    SDL_Rect left_panel = {margin, video_y, pane_w, video_h};
    SDL_Rect right_panel = {margin + pane_w + gap, video_y, pane_w, video_h};
    draw_panel(renderer, left_panel, panel, border);
    draw_panel(renderer, right_panel, panel, border);

    draw_text(renderer, left_panel.x, margin, 2, "ORIGINAL", accent);
    draw_text(renderer, right_panel.x, margin, 2, "ENCODED + DECODED", accent);

    SDL_Rect src_dst = fit_rect(left_panel.x + 8, left_panel.y + 8,
                                left_panel.w - 16, left_panel.h - 16,
                                state->width, state->height);
    SDL_Rect rec_dst = fit_rect(right_panel.x + 8, right_panel.y + 8,
                                right_panel.w - 16, right_panel.h - 16,
                                state->width, state->height);

    SDL_RenderCopy(renderer, state->gui.source_tex, NULL, &src_dst);
    SDL_RenderCopy(renderer, state->gui.recon_tex, NULL, &rec_dst);

    SDL_Rect metrics = {
        margin,
        win_h - bottom_h,
        win_w - margin * 2,
        bottom_h - margin
    };
    draw_panel(renderer, metrics, panel, border);

    double fps = state->fps_den > 0 ? (double)state->fps_num / state->fps_den : 0.0;
    double actual_kbps = 0.0;
    double ratio = 0.0;
    if (state->frames_in > 0 && fps > 0.0) {
        actual_kbps = (state->total_bytes * 8.0 * fps) /
                      (state->frames_in * 1000.0);
    }
    if (state->total_bytes > 0) {
        double raw_bytes = (double)state->frames_in *
                           state->width * state->height * 1.5;
        ratio = raw_bytes / (double)state->total_bytes;
    }

    int failures = state->dropped + state->unmatched;
    double stability = state->frames_in > 0
                       ? 100.0 - (100.0 * failures / state->frames_in)
                       : 100.0;
    if (stability < 0.0)
        stability = 0.0;

    int x = metrics.x + 14;
    int y = metrics.y + 14;
    int scale = win_w < 1000 ? 1 : 2;
    int line = scale == 1 ? 13 : 19;

    draw_textf(renderer, x, y, scale, text,
               "INPUT %dx%d @ %.2f FPS  TARGET %d KBPS  ACTUAL %.1f KBPS  RATIO %.1fX",
               state->width, state->height, fps, state->target_bitrate / 1000,
               actual_kbps, ratio);

    y += line;
    draw_textf(renderer, x, y, scale, text,
               "FRAMES IN %d  OUT %d  PKTS %d  QUEUE %d  STABILITY %.1f%%",
               state->frames_in, state->frames_out, state->packets,
               state->originals.count, stability);

    y += line;
    draw_textf(renderer, x, y, scale, text,
               "LATENCY CUR %.2f MS  AVG %.2f MS  JITTER %.2f MS  MAX %.2f MS",
               state->latency.count ? state->latency.last : 0.0,
               state->latency.count ? state->latency.mean : 0.0,
               stats_stddev(&state->latency),
               state->latency.count ? state->latency.max : 0.0);

    y += line;
    draw_textf(renderer, x, y, scale, text,
               "PSNR Y %.2f  U %.2f  V %.2f  AVG %.2f DB  MEAN %.2f DB  MIN %.2f DB",
               state->last_psnr.y, state->last_psnr.u,
               state->last_psnr.v, state->last_psnr.weighted,
               state->psnr_weighted.count ? state->psnr_weighted.mean : 0.0,
               state->psnr_weighted.count ? state->psnr_weighted.min : 0.0);

    y += line;
    draw_textf(renderer, x, y, scale, state->done ? accent : dim,
               "STATUS %s  LOW_DELAY %s  COMPARED %d  DROPPED %d  UNMATCHED %d",
               state->done ? "DONE" : "RUNNING",
               state->low_delay ? "ON" : "OFF",
               state->compared, state->dropped, state->unmatched);

    SDL_RenderPresent(renderer);
}

static int gui_init(gui_ctx *gui, int width, int height)
{
    memset(gui, 0, sizeof(*gui));
    gui->video_w = width;
    gui->video_h = height;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return -1;
    }

    gui->window = SDL_CreateWindow("rkvc visual compare",
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   WINDOW_DEFAULT_W,
                                   WINDOW_DEFAULT_H,
                                   SDL_WINDOW_RESIZABLE);
    if (!gui->window) {
        fprintf(stderr, "SDL window failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetWindowMinimumSize(gui->window, WINDOW_MIN_W, WINDOW_MIN_H);

    gui->renderer = SDL_CreateRenderer(gui->window, -1,
                                       SDL_RENDERER_ACCELERATED |
                                       SDL_RENDERER_PRESENTVSYNC);
    if (!gui->renderer) {
        gui->renderer = SDL_CreateRenderer(gui->window, -1,
                                           SDL_RENDERER_SOFTWARE);
    }
    if (!gui->renderer) {
        fprintf(stderr, "SDL renderer failed: %s\n", SDL_GetError());
        return -1;
    }

    gui->source_tex = SDL_CreateTexture(gui->renderer, SDL_PIXELFORMAT_IYUV,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        width, height);
    gui->recon_tex = SDL_CreateTexture(gui->renderer, SDL_PIXELFORMAT_IYUV,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       width, height);
    if (!gui->source_tex || !gui->recon_tex) {
        fprintf(stderr, "SDL texture failed: %s\n", SDL_GetError());
        return -1;
    }

    return 0;
}

static void gui_destroy(gui_ctx *gui)
{
    if (!gui)
        return;

    if (gui->source_tex)
        SDL_DestroyTexture(gui->source_tex);
    if (gui->recon_tex)
        SDL_DestroyTexture(gui->recon_tex);
    if (gui->renderer)
        SDL_DestroyRenderer(gui->renderer);
    if (gui->window)
        SDL_DestroyWindow(gui->window);
    scratch_free(&gui->scratch);
    SDL_Quit();
    memset(gui, 0, sizeof(*gui));
}

static int poll_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT)
            return 0;
        if (ev.type == SDL_KEYDOWN) {
            if (ev.key.keysym.sym == SDLK_ESCAPE ||
                ev.key.keysym.sym == SDLK_q) {
                return 0;
            }
        }
    }
    return 1;
}

static void pace_frame(app_state *state, int fast)
{
    if (fast || state->frame_interval_ms <= 0.0)
        return;

    double now = now_ms();
    if (state->last_present_ms <= 0.0) {
        state->last_present_ms = now;
        return;
    }

    double target = state->last_present_ms + state->frame_interval_ms;
    if (target > now) {
        double sleep_ms = target - now;
        if (sleep_ms > 1.0)
            SDL_Delay((Uint32)sleep_ms);
    }
    state->last_present_ms = now_ms();
}

static void handle_recon_frame(app_state *state, rkvc_frame *recon,
                               const app_options *opt)
{
    rkvc_frame *source = NULL;
    double input_ms = 0.0;
    int source_index = 0;

    state->frames_out++;

    if (!queue_pop(&state->originals, &source, &input_ms, &source_index)) {
        state->unmatched++;
        texture_update_nv12(&state->gui, state->gui.recon_tex, recon);
        render_state(state);
        return;
    }

    double latency_ms = now_ms() - input_ms;
    stats_add(&state->latency, latency_ms);
    state->compared++;

    texture_update_nv12(&state->gui, state->gui.source_tex, source);
    texture_update_nv12(&state->gui, state->gui.recon_tex, recon);

    if (opt->psnr_step <= 1 || source_index % opt->psnr_step == 0) {
        psnr_result psnr;
        if (calc_yuv420_psnr(source, recon, &psnr) == 0) {
            state->last_psnr = psnr;
            stats_add(&state->psnr_weighted, psnr.weighted);
        }
    }

    rkvc_frame_unref(source);
    render_state(state);
    pace_frame(state, opt->fast);
}

static int receive_preview_frames(app_state *state, rkvc_decoder *recon_dec,
                                  const app_options *opt)
{
    rkvc_frame *recon = NULL;
    rkvc_err err;

    while ((err = rkvc_decoder_receive_frame(recon_dec, &recon)) == RKVC_OK) {
        handle_recon_frame(state, recon, opt);
        rkvc_frame_unref(recon);
        if (!state->running)
            return 0;
    }

    if (err != RKVC_ERR_AGAIN && err != RKVC_ERR_EOF) {
        fprintf(stderr, "receive preview frame failed: %s\n",
                rkvc_err_str(err));
        return -1;
    }

    return 0;
}

static int send_packet_to_preview(app_state *state, rkvc_decoder *recon_dec,
                                  const rkvc_packet *pkt,
                                  const app_options *opt)
{
    for (int attempt = 0; attempt < 4; attempt++) {
        rkvc_err err = rkvc_decoder_send_packet(recon_dec, pkt->data,
                                                pkt->size, pkt->pts,
                                                pkt->dts);
        if (err == RKVC_OK)
            return 0;

        if (err == RKVC_ERR_AGAIN) {
            if (receive_preview_frames(state, recon_dec, opt) != 0)
                return -1;
            continue;
        }

        fprintf(stderr, "send packet to preview decoder failed: %s\n",
                rkvc_err_str(err));
        return -1;
    }

    fprintf(stderr, "send packet to preview decoder stalled after draining frames\n");
    return -1;
}

static int pump_encoder_decoder(app_state *state, rkvc_encoder *enc,
                                rkvc_decoder *recon_dec,
                                const app_options *opt)
{
    rkvc_packet pkt;
    rkvc_err err;

    while ((err = rkvc_encoder_receive_packet(enc, &pkt)) == RKVC_OK) {
        state->packets++;
        state->total_bytes += (uint64_t)pkt.size;

        if (send_packet_to_preview(state, recon_dec, &pkt, opt) != 0)
            return -1;

        if (receive_preview_frames(state, recon_dec, opt) != 0)
            return -1;
        if (!state->running)
            return 0;
    }

    if (err != RKVC_ERR_AGAIN && err != RKVC_ERR_EOF) {
        fprintf(stderr, "receive encoded packet failed: %s\n",
                rkvc_err_str(err));
        return -1;
    }

    return 0;
}

static int send_frame_to_encoder(app_state *state, rkvc_encoder *enc,
                                 rkvc_decoder *recon_dec, rkvc_frame *frame,
                                 const app_options *opt)
{
    for (int attempt = 0; attempt < 4; attempt++) {
        rkvc_err err = rkvc_encoder_send_frame(enc, frame);
        if (err == RKVC_OK)
            return 0;

        if (err == RKVC_ERR_AGAIN) {
            if (pump_encoder_decoder(state, enc, recon_dec, opt) != 0)
                return -1;
            continue;
        }

        fprintf(stderr, "send frame to encoder failed: %s\n",
                rkvc_err_str(err));
        return -1;
    }

    fprintf(stderr, "send frame to encoder stalled after draining packets\n");
    return -1;
}

static void print_usage(const char *prog)
{
    printf("visual_compare - SDL2 source/reconstructed preview\n"
           "Usage: %s -i input.h265 [-b bps] [-n frames] [-l] [-f] [-p step]\n"
           "  -i, --input FILE       input HEVC/H.265 file\n"
           "  -b, --bitrate BPS      target encode bitrate (default 4000000)\n"
           "  -n, --frames N         stop after N input frames (default: all)\n"
           "  -l, --low-delay        enable low-delay preview decoder\n"
           "  -f, --fast             process as fast as possible\n"
           "  -p, --psnr-step N      compute PSNR every N frames (default 1)\n",
           prog);
}

static int parse_options(int argc, char **argv, app_options *opt)
{
    memset(opt, 0, sizeof(*opt));
    opt->bitrate = 4000000;
    opt->psnr_step = 1;

    static struct option long_opts[] = {
        {"input", required_argument, 0, 'i'},
        {"bitrate", required_argument, 0, 'b'},
        {"frames", required_argument, 0, 'n'},
        {"low-delay", no_argument, 0, 'l'},
        {"fast", no_argument, 0, 'f'},
        {"psnr-step", required_argument, 0, 'p'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "i:b:n:lfp:h", long_opts, NULL)) != -1) {
        switch (c) {
        case 'i': opt->input = optarg; break;
        case 'b': opt->bitrate = atoi(optarg); break;
        case 'n': opt->max_frames = atoi(optarg); break;
        case 'l': opt->low_delay = 1; break;
        case 'f': opt->fast = 1; break;
        case 'p': opt->psnr_step = atoi(optarg); break;
        case 'h':
            print_usage(argv[0]);
            return 1;
        default:
            return -1;
        }
    }

    if (!opt->input || opt->bitrate <= 0 || opt->max_frames < 0 ||
        opt->psnr_step <= 0) {
        print_usage(argv[0]);
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    app_options opt;
    int parse = parse_options(argc, argv, &opt);
    if (parse != 0)
        return parse > 0 ? 0 : 1;

    rkvc_err err = rkvc_init();
    if (err != RKVC_OK) {
        fprintf(stderr, "rkvc init failed: %s\n", rkvc_err_str(err));
        return 1;
    }

    rkvc_decoder *source_dec = NULL;
    rkvc_encoder *enc = NULL;
    rkvc_decoder *recon_dec = NULL;
    app_state state;
    memset(&state, 0, sizeof(state));
    state.running = 1;

    rkvc_decoder_config source_cfg = rkvc_decoder_config_defaults();
    source_cfg.output_format = RKVC_PIX_FMT_NV12;
    err = rkvc_decoder_open_file(&source_dec, &source_cfg, opt.input);
    if (err != RKVC_OK) {
        fprintf(stderr, "open input decoder failed: %s\n", rkvc_err_str(err));
        goto fail;
    }

    int fps_num = 30;
    int fps_den = 1;
    err = rkvc_decoder_get_video_info(source_dec, &state.width, &state.height,
                                      &fps_num, &fps_den);
    if (err != RKVC_OK || state.width <= 0 || state.height <= 0) {
        fprintf(stderr, "failed to read input video info\n");
        goto fail;
    }
    state.fps_num = fps_num > 0 ? fps_num : 30;
    state.fps_den = fps_den > 0 ? fps_den : 1;
    state.target_bitrate = opt.bitrate;
    state.low_delay = opt.low_delay;
    state.frame_interval_ms = 1000.0 * state.fps_den / state.fps_num;
    state.start_ms = now_ms();

    rkvc_encoder_config enc_cfg = rkvc_encoder_config_defaults();
    enc_cfg.width = state.width;
    enc_cfg.height = state.height;
    enc_cfg.fps_num = state.fps_num;
    enc_cfg.fps_den = state.fps_den;
    enc_cfg.bitrate = opt.bitrate;
    enc_cfg.input_format = RKVC_PIX_FMT_NV12;

    err = rkvc_encoder_open(&enc, &enc_cfg);
    if (err != RKVC_OK) {
        fprintf(stderr, "open encoder failed: %s\n", rkvc_err_str(err));
        goto fail;
    }

    rkvc_decoder_config recon_cfg = rkvc_decoder_config_defaults();
    recon_cfg.output_format = RKVC_PIX_FMT_NV12;
    recon_cfg.low_delay = opt.low_delay;
    err = rkvc_decoder_open(&recon_dec, &recon_cfg);
    if (err != RKVC_OK) {
        fprintf(stderr, "open preview decoder failed: %s\n", rkvc_err_str(err));
        goto fail;
    }

    queue_init(&state.originals, QUEUE_CAPACITY);
    if (state.originals.cap <= 0) {
        fprintf(stderr, "frame queue allocation failed\n");
        goto fail;
    }

    if (gui_init(&state.gui, state.width, state.height) != 0)
        goto fail;

    render_state(&state);

    while (state.running) {
        state.running = poll_events();
        if (!state.running)
            break;

        if (opt.max_frames > 0 && state.frames_in >= opt.max_frames)
            break;

        err = rkvc_decoder_read_packet(source_dec);
        if (err == RKVC_ERR_EOF)
            break;
        if (err != RKVC_OK) {
            fprintf(stderr, "read input packet failed: %s\n", rkvc_err_str(err));
            goto fail;
        }

        rkvc_frame *frame = NULL;
        while (rkvc_decoder_receive_frame(source_dec, &frame) == RKVC_OK) {
            if (opt.max_frames > 0 && state.frames_in >= opt.max_frames) {
                rkvc_frame_unref(frame);
                break;
            }

            int frame_index = state.frames_in;
            double input_ms = now_ms();
            rkvc_frame_set_pts(frame, frame_index);
            state.frames_in++;

            texture_update_nv12(&state.gui, state.gui.source_tex, frame);

            int send_ok = send_frame_to_encoder(&state, enc, recon_dec,
                                                frame, &opt);
            if (send_ok == 0) {
                state.dropped += queue_push(&state.originals, frame,
                                            input_ms, frame_index);
            }
            rkvc_frame_unref(frame);
            if (send_ok != 0)
                goto fail;

            if (pump_encoder_decoder(&state, enc, recon_dec, &opt) != 0)
                goto fail;

            render_state(&state);
            if (!state.running)
                break;
        }
    }

    if (state.running) {
        rkvc_encoder_drain(enc);
        if (pump_encoder_decoder(&state, enc, recon_dec, &opt) != 0)
            goto fail;

        rkvc_decoder_drain(recon_dec);
        if (receive_preview_frames(&state, recon_dec, &opt) != 0)
            goto fail;
    }

    state.done = 1;
    render_state(&state);

    while (state.running) {
        state.running = poll_events();
        render_state(&state);
        SDL_Delay(16);
    }

    gui_destroy(&state.gui);
    queue_clear(&state.originals);
    rkvc_decoder_close(recon_dec);
    rkvc_encoder_close(enc);
    rkvc_decoder_close(source_dec);
    rkvc_deinit();
    return 0;

fail:
    gui_destroy(&state.gui);
    queue_clear(&state.originals);
    rkvc_decoder_close(recon_dec);
    rkvc_encoder_close(enc);
    rkvc_decoder_close(source_dec);
    rkvc_deinit();
    return 1;
}
