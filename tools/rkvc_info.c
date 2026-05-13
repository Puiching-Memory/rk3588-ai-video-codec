/**
 * @file rkvc_info.c
 * @brief 硬件能力查询工具。
 *
 * 用法:
 *   rkvc_info
 *   rkvc_info --version
 */

#include "rkvc/rkvc.h"
#include <stdio.h>
#include <string.h>
#include <getopt.h>

static void usage(void) {
    printf(
        "rkvc_info — RK3588 VPU 硬件能力查询工具\n"
        "\n"
        "用法:\n"
        "  rkvc_info [选项]\n"
        "\n"
        "选项:\n"
        "  -v, --version    显示版本\n"
        "  -j, --json       JSON 格式输出\n"
        "  -h, --help       显示帮助\n"
    );
}

int main(int argc, char **argv) {
    int show_version = 0, json_output = 0;

    static struct option long_opts[] = {
        {"version", no_argument, 0, 'v'},
        {"json",    no_argument, 0, 'j'},
        {"help",    no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "vjh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'v': show_version = 1; break;
        case 'j': json_output = 1; break;
        case 'h': usage(); return 0;
        default: usage(); return 1;
        }
    }

    if (show_version) {
        printf("rkvc %s (0x%06x)\n", rkvc_version(), rkvc_version_number());
        return 0;
    }

    rkvc_init();

    rkvc_caps caps;
    rkvc_err err = rkvc_query_caps(&caps);
    if (err != RKVC_OK) {
        fprintf(stderr, "能力查询失败: %s\n", rkvc_err_str(err));
        return 1;
    }

    if (json_output) {
        printf("{\n");
        printf("  \"version\": \"%s\",\n", rkvc_version());
        printf("  \"rkmpp_enc\": %s,\n", caps.has_rkmpp_enc ? "true" : "false");
        printf("  \"rkmpp_dec\": %s,\n", caps.has_rkmpp_dec ? "true" : "false");
        printf("  \"dma_heap\": %s,\n", caps.has_dma_heap ? "true" : "false");
        printf("  \"rga\": %s,\n", caps.has_rga ? "true" : "false");
        printf("  \"max_width\": %d,\n", caps.max_width);
        printf("  \"max_height\": %d\n", caps.max_height);
        printf("}\n");
    } else {
        printf("rkvc %s\n", rkvc_version());
        printf("\n硬件能力:\n");
        printf("  RKMPP 编码: %s\n", caps.has_rkmpp_enc ? "可用" : "不可用");
        printf("  RKMPP 解码: %s\n", caps.has_rkmpp_dec ? "可用" : "不可用");
        printf("  DMA Heap:   %s\n", caps.has_dma_heap ? "可用" : "不可用");
        printf("  RGA 2D:     %s\n", caps.has_rga ? "可用" : "不可用");
        printf("  最大分辨率: %dx%d\n", caps.max_width, caps.max_height);
    }

    rkvc_deinit();
    return 0;
}
