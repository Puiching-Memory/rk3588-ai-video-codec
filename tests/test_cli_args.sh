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

run_expect_fail "encode missing output" "用法|OUTPUT" \
    "$ENC" --testsrc -n 1 -s 640x480
run_expect_fail "encode missing input" "需要 -i|--stdin|--testsrc|输入源" \
    "$ENC" -o /tmp/rkvc-cli-should-not-exist.h265 -s 640x480
printf '\0\0\0\1\x67\xf4\0\x0a\x91\x96\x9e\xc0\x44\0\0\x03\0\x04\0\0\x03\0\x0a\x3c\x48\x9a\x80\0\0\0\1\x68\xce\x0f\x19\x20\0\0\1\x06' > "$TMPDIR/compressed-input.h264"
run_expect_fail "encode compressed h264 input" "压缩码流|原始 NV12|rkvc_decode|transcode" \
    "$ENC" -i "$TMPDIR/compressed-input.h264" -o "$TMPDIR/unused.h265" -s 640x480
run_expect_fail "decode missing input" "用法|INPUT" \
    "$DEC" -o /tmp/rkvc-cli-should-not-exist.nv12
run_expect_fail "decode stdin missing size" "stdin 模式需要" \
    "$DEC" --stdin -o /tmp/rkvc-cli-should-not-exist.nv12
run_expect_fail "decode missing file" "not found|No such file|codec or device not found" \
    "$DEC" -i /tmp/rkvc-definitely-missing-input.h265 -o /tmp/rkvc-cli-should-not-exist.nv12

echo "CLI argument negative tests passed"
