#!/bin/bash
# scripts/build-common.sh — 统一限制编译并行度（默认 4，上限 4）
#
# 用法（在其它构建脚本中）:
#   source "$(dirname "$0")/build-common.sh"
#   rkvc_limit_build_jobs
#
# 可通过环境变量 BUILD_JOBS 下调（如 2），但不会超过 RKVC_BUILD_JOBS_MAX（默认 4）。

rkvc_limit_build_jobs() {
    local max_jobs="${RKVC_BUILD_JOBS_MAX:-4}"
    local jobs="${BUILD_JOBS:-4}"

    if ! [[ "$jobs" =~ ^[0-9]+$ ]] || [ "$jobs" -lt 1 ]; then
        jobs=1
    fi
    if [ "$jobs" -gt "$max_jobs" ]; then
        jobs="$max_jobs"
    fi

    BUILD_JOBS="$jobs"
    export BUILD_JOBS
}
