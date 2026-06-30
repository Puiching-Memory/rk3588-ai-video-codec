#!/usr/bin/env bash
# RGA 硬件缩放推广门禁（RK3588 实机）
#
# 用法:
#   ./scripts/test-rga.sh
#   RKVC_RGA_SOAK_FRAMES=500 ./scripts/test-rga.sh
#
# 通过标准:
#   - /dev/rga 可访问
#   - test_scale 全部硬件用例通过（含 1080p↔360p bench 路径）
#   - 可选 soak：连续多帧上采样无挂死

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=build-common.sh
source "$SCRIPT_DIR/build-common.sh" 2>/dev/null || true
rkvc_limit_build_jobs 2>/dev/null || true

BUILD_DIR="${RKVC_BUILD_DIR:-$ROOT_DIR/build}"
SOAK_FRAMES="${RKVC_RGA_SOAK_FRAMES:-200}"

if [[ ! -e /dev/rga ]]; then
    echo "[error] /dev/rga 不存在，跳过 RGA 门禁" >&2
    exit 1
fi

if [[ ! -r /dev/rga || ! -w /dev/rga ]]; then
    echo "[error] /dev/rga 权限不足 (需要读写)" >&2
    exit 1
fi

echo "[info] RGA device: $(ls -la /dev/rga)"
echo "[info] build dir:  $BUILD_DIR"
echo "[info] soak frames: $SOAK_FRAMES"

if [[ ! -x "$BUILD_DIR/test_scale" ]]; then
    echo "[info] building test_scale ..."
    if [[ -d "$BUILD_DIR" ]]; then
        cmake --build "$BUILD_DIR" --target test_scale -j"${BUILD_JOBS:-4}"
    else
        cmake --preset tests -S "$ROOT_DIR" -B "$ROOT_DIR/build-tests"
        cmake --build "$ROOT_DIR/build-tests" --target test_scale -j"${BUILD_JOBS:-4}"
        BUILD_DIR="$ROOT_DIR/build-tests"
    fi
fi

export RKVC_RUN_HARDWARE_TESTS=1
export RKVC_RGA_SOAK_FRAMES="$SOAK_FRAMES"

echo "[run] test_scale (hardware + soak=$SOAK_FRAMES)"
"$BUILD_DIR/test_scale"

if command -v ctest >/dev/null 2>&1 && [[ -f "$BUILD_DIR/CTestTestfile.cmake" ]]; then
    echo "[run] ctest -R test_scale"
    ctest --test-dir "$BUILD_DIR" -j1 -R '^test_scale$' --output-on-failure
fi

if [[ -n "${RKVC_TEST_RAW_NV12:-}" && -r "${RKVC_TEST_RAW_NV12}" && -x "$BUILD_DIR/test_hardware" ]]; then
    echo "[run] test_session_encode_decode_upscale_3x (raw=$RKVC_TEST_RAW_NV12)"
    "$BUILD_DIR/test_hardware" test_session_encode_decode_upscale_3x
fi

echo "[ok] RGA gate passed"
