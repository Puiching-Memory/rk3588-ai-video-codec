#!/bin/bash
set -euo pipefail

BUILD_DIR="${1:?usage: $0 <build-dir>}"
ENC="$BUILD_DIR/rkvc_encode"
DEC="$BUILD_DIR/rkvc_decode"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

run_expect_fail() {
    local name="$1"
    local pattern="$2"
    shift 2

    local output status
    set +e
    output=$("$@" 2>&1)
    status=$?
    set -e

    if [ "$status" -eq 0 ]; then
        echo "FAIL $name: command unexpectedly succeeded"
        printf '%s\n' "$output"
        exit 1
    fi

    if ! printf '%s\n' "$output" | grep -Eq "$pattern"; then
        echo "FAIL $name: output did not match $pattern"
        printf '%s\n' "$output"
        exit 1
    fi
}

run_expect_fail "encode missing output" "usage|raw.nv12" \
    "$ENC" -i /dev/null -s 640x480
run_expect_fail "encode missing input" "usage|raw.nv12" \
    "$ENC" -o "$TMPDIR/out.mp4" -s 640x480
run_expect_fail "decode missing input" "usage|decode|in.mp4" \
    "$DEC" -o "$TMPDIR/out.nv12"
run_expect_fail "decode missing output" "usage|out.nv12" \
    "$DEC" -i /dev/null

echo "CLI argument negative tests passed"
