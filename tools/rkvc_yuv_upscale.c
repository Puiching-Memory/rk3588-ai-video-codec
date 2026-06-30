/**
 * @file rkvc_yuv_upscale.c
 * @brief YUV420p 文件批量上采样（RGA 硬件，供 bench 等调用）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rkvc/rkvc.h"

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s --in IN.yuv --out OUT.yuv --sw W --sh H --dw W --dh H "
            "--algo nearest|bilinear|bicubic [--frames N]\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *in_path = NULL, *out_path = NULL, *algo_name = "bilinear";
    int sw = 0, sh = 0, dw = 0, dh = 0, frames = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--in") && i + 1 < argc)
            in_path = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out_path = argv[++i];
        else if (!strcmp(argv[i], "--sw") && i + 1 < argc)
            sw = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--sh") && i + 1 < argc)
            sh = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dw") && i + 1 < argc)
            dw = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dh") && i + 1 < argc)
            dh = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--algo") && i + 1 < argc)
            algo_name = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!in_path || !out_path || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) {
        usage(argv[0]);
        return 1;
    }

    rkvc_upscale_algo algo;
    if (rkvc_upscale_algo_from_name(algo_name, &algo) != 0) {
        fprintf(stderr, "unknown algo: %s\n", algo_name);
        return 1;
    }

    rkvc_caps caps;
    if (rkvc_query_caps(&caps) != RKVC_OK || !caps.has_rga) {
        fprintf(stderr, "RGA not available\n");
        return 1;
    }

    const size_t src_frame = (size_t)sw * (size_t)sh * 3 / 2;
    const size_t dst_frame = (size_t)dw * (size_t)dh * 3 / 2;
    uint8_t *src_buf = malloc(src_frame);
    uint8_t *dst_buf = malloc(dst_frame);
    if (!src_buf || !dst_buf) {
        free(src_buf);
        free(dst_buf);
        return 1;
    }

    FILE *fin = fopen(in_path, "rb");
    FILE *fout = fopen(out_path, "wb");
    if (!fin || !fout) {
        perror("fopen");
        free(src_buf);
        free(dst_buf);
        fclose(fin);
        fclose(fout);
        return 1;
    }

    int n = 0;
    while (fread(src_buf, 1, src_frame, fin) == src_frame) {
        if (rkvc_upscale_yuv420p(src_buf, dst_buf, sw, sh, dw, dh, algo) != RKVC_OK) {
            fprintf(stderr, "rkvc_upscale_yuv420p failed on frame %d\n", n);
            fclose(fin);
            fclose(fout);
            free(src_buf);
            free(dst_buf);
            return 1;
        }
        if (fwrite(dst_buf, 1, dst_frame, fout) != dst_frame) {
            perror("fwrite");
            fclose(fin);
            fclose(fout);
            free(src_buf);
            free(dst_buf);
            return 1;
        }
        n++;
        if (frames > 0 && n >= frames)
            break;
    }

    fclose(fin);
    fclose(fout);
    free(src_buf);
    free(dst_buf);
    return 0;
}
