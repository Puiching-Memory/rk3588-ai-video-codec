/**
 * @file rkvc_bench.c
 * @brief v2 Session E2E 基准：对比 REALTIME / BALANCED / QUALITY 路线。
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/time.h>

typedef struct {
    const char *input;
    const char *output_dir;
    int width;
    int height;
    int frames;
} bench_opts;

static double bench_policy(rkvc_policy policy, const bench_opts *o)
{
    char out[512];
    snprintf(out, sizeof(out), "%s/bench_%s.mp4", o->output_dir,
             rkvc_policy_name(policy));

    rkvc_pipeline_desc d;
    rkvc_pipeline_from_template(RKVC_TEMPLATE_FILE_TRANSCODE, &d);
    d.policy      = policy;
    d.input_path  = o->input;
    d.output_path = out;
    d.width       = o->width;
    d.height      = o->height;

    rkvc_session *s = NULL;
    if (rkvc_session_create(&d, &s) != RKVC_OK)
        return -1.0;

    struct timeval t0, t1;
    gettimeofday(&t0, NULL);
    rkvc_err err = rkvc_session_run_file(s);
    gettimeofday(&t1, NULL);

    rkvc_session_stats st;
    rkvc_session_get_stats(s, &st);
    rkvc_session_destroy(s);

    if (err != RKVC_OK)
        return -1.0;

    double sec = (t1.tv_sec - t0.tv_sec) +
                 (t1.tv_usec - t0.tv_usec) / 1e6;
    return sec > 0 ? st.frames_out / sec : st.avg_fps;
}

int main(int argc, char **argv)
{
    bench_opts o = {
        .input = "tests/fixtures/sample.h264.mp4",
        .output_dir = "/tmp/rkvc_bench",
        .width = 1920,
        .height = 1080,
        .frames = 300,
    };

    static struct option opts[] = {
        {"input", required_argument, 0, 'i'},
        {"output", required_argument, 0, 'o'},
        {"size", required_argument, 0, 's'},
        {0, 0, 0, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "i:o:s:", opts, NULL)) != -1) {
        if (c == 'i') o.input = optarg;
        else if (c == 'o') o.output_dir = optarg;
        else if (c == 's') sscanf(optarg, "%dx%d", &o.width, &o.height);
    }

    rkvc_init();
    mkdir(o.output_dir, 0755);

    printf("rkvc v2 session E2E bench (input=%s)\n", o.input);
    printf("  REALTIME (H.264): %.1f fps\n",
           bench_policy(RKVC_POLICY_REALTIME, &o));
    printf("  BALANCED (HEVC):  %.1f fps\n",
           bench_policy(RKVC_POLICY_BALANCED, &o));
    printf("  QUALITY (AV1):    %.1f fps\n",
           bench_policy(RKVC_POLICY_QUALITY, &o));
    return 0;
}
