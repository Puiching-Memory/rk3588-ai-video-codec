#!/bin/bash
set -euo pipefail

BUILD_DIR="${1:?usage: $0 <build-dir>}"
BENCH="$BUILD_DIR/rkvc_bench"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

if [ ! -x "$BENCH" ]; then
    echo "FAIL bench permission: missing executable $BENCH"
    echo "hint: run this against a tools-enabled test build such as the full-tests preset"
    exit 1
fi

mkdir -p "$TMPDIR/dev/dma_heap"
touch "$TMPDIR/dev/mpp_service"
touch "$TMPDIR/dev/dma_heap/cma"

set +e
output=$(RKVC_TEST_DEV_ROOT="$TMPDIR" "$BENCH" -s 640x480 -n 3 --encode-only -o "$TMPDIR/out" 2>&1)
status=$?
set -e

if [ "$status" -eq 0 ]; then
    echo "FAIL bench permission: command unexpectedly succeeded"
    echo "hint: this test requires RKVC_ENABLE_FAULT_INJECTION=ON"
    printf '%s\n' "$output"
    exit 1
fi

if ! printf '%s\n' "$output" | grep -q "\[FAIL\] encode"; then
    echo "FAIL bench permission: missing [FAIL] encode"
    printf '%s\n' "$output"
    exit 1
fi

if ! printf '%s\n' "$output" | grep -q "device permission denied"; then
    echo "FAIL bench permission: missing permission error"
    printf '%s\n' "$output"
    exit 1
fi

echo "bench permission failure propagation test passed"
