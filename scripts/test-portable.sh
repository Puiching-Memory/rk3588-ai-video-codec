#!/bin/bash
# scripts/test-portable.sh — 测试可移植包
#
# 用法:
#   ./scripts/test-portable.sh <portable-package-dir>
#   ./test.sh                         # 在可移植包目录内一键自测
#
# 测试项目:
#   1. 文件完整性
#   2. 动态库依赖与包内库来源
#   3. RPATH/RUNPATH 自包含
#   4. CLI 功能与 JSON 字段
#   5. 编码、解码、管道模式、本机网络回环
#   6. 开发头文件与 pkg-config
#   7. CLI 参数错误与包结构负向测试

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'
PASS=0
FAIL=0

pass() { PASS=$((PASS+1)); echo -e "${GREEN}✓${NC} $1"; }
fail() { FAIL=$((FAIL+1)); echo -e "${RED}✗${NC} $1"; }
warn() { echo -e "${YELLOW}!${NC} $1"; }
show_output() {
    local title="$1"
    local output="$2"

    echo "  --- $title 输出 ---"
    if [ -n "$output" ]; then
        printf '%s\n' "$output" | sed 's/^/  | /'
    else
        echo "  | <无输出>"
    fi
    echo "  --- end ---"
}
run_capture() {
    local __out_var="$1"
    shift

    local output
    set +e
    output=$("$@" 2>&1)
    local status=$?
    set -e
    printf -v "$__out_var" '%s' "$output"
    return "$status"
}
capture_run() {
    local __status_var="$1"
    local __out_var="$2"
    shift 2

    local status
    if run_capture "$__out_var" "$@"; then
        status=0
    else
        status=$?
    fi
    printf -v "$__status_var" '%s' "$status"
}
extract_frames() {
    local output="$1"
    local marker="$2"
    local line

    while IFS= read -r line; do
        if [[ "$line" == *"$marker"* && "$line" =~ ([0-9]+)[[:space:]]*帧 ]]; then
            printf '%s 帧' "${BASH_REMATCH[1]}"
            return
        fi
    done <<< "$output"

    if [[ "$output" =~ ([0-9]+)[[:space:]]*帧 ]]; then
        printf '%s 帧' "${BASH_REMATCH[1]}"
    else
        printf '未知帧数'
    fi
}
file_size() {
    stat -c '%s' "$1" 2>/dev/null || wc -c < "$1"
}
check_runpath_contains() {
    local file="$1"
    local label="$2"
    local expected="$3"
    local output

    output=$(readelf -d "$file" 2>&1 || true)
    if echo "$output" | grep -Eq 'RPATH|RUNPATH'; then
        if echo "$output" | grep -Fq "$expected"; then
            pass "$label: RPATH 包含 $expected"
        else
            fail "$label: RPATH 未包含 $expected"
            show_output "readelf -d $label" "$output"
        fi
        if echo "$output" | grep -q "$PKG_DIR"; then
            fail "$label: RPATH 含包目录绝对路径"
            show_output "readelf -d $label" "$output"
        elif echo "$output" | grep -q "/root/rk3588-ai-video-codec"; then
            fail "$label: RPATH 含构建机绝对路径"
            show_output "readelf -d $label" "$output"
        fi
    else
        fail "$label: 缺少 RPATH/RUNPATH"
        show_output "readelf -d $label" "$output"
    fi
}
expect_runpath_check_fail() {
    local label="$1"
    shift

    local before="$FAIL"
    check_runpath_contains "$@"
    if [ "$FAIL" -gt "$before" ]; then
        FAIL="$before"
        pass "$label"
    else
        fail "$label: RPATH 检测未报告失败"
    fi
}
expect_command_fail() {
    local label="$1"
    local pattern="$2"
    shift 2

    local status output
    set +e
    output=$("$@" 2>&1)
    status=$?
    set -e

    if [ "$status" -eq 0 ]; then
        fail "$label: 命令意外成功"
        show_output "$label" "$output"
    elif echo "$output" | grep -Eq "$pattern"; then
        pass "$label"
    else
        fail "$label: 输出未匹配 $pattern (exit=$status)"
        show_output "$label" "$output"
    fi
}
copy_package_tree() {
    local dst="$1"

    rm -rf "$dst"
    mkdir -p "$dst"
    (cd "$PKG_DIR" && tar cf - .) | (cd "$dst" && tar xf -)
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [[ $# -gt 0 ]]; then
    PKG_DIR="$1"
elif [[ -x "$SCRIPT_DIR/bin/rkvc_info" && -d "$SCRIPT_DIR/lib" ]]; then
    PKG_DIR="$SCRIPT_DIR"
else
    echo "用法: $0 <package-dir>"
    echo "或在可移植包目录内运行: ./test.sh"
    exit 2
fi

PKG_DIR="$(cd "$PKG_DIR" && pwd)"

echo "=== 测试可移植包: $PKG_DIR ==="
echo ""

# 1. 文件完整性
echo "--- 文件完整性 ---"
for f in bin/rkvc_encode bin/rkvc_decode bin/rkvc_info bin/rkvc_bench; do
    if [ -x "$PKG_DIR/$f" ]; then
        pass "存在且可执行: $f"
    elif [ -e "$PKG_DIR/$f" ]; then
        fail "存在但不可执行: $f"
    else
        fail "缺失: $f"
    fi
done
for f in lib/librkvc.so include/rkvc/rkvc.h; do
    if [ -e "$PKG_DIR/$f" ]; then
        pass "存在: $f"
    else
        fail "缺失: $f"
    fi
done
# ffmpeg 库使用通配符匹配 (版本号随 ffmpeg 版本变化)
for name in libavcodec libavformat libavutil; do
    if ls "$PKG_DIR/lib/${name}.so."* >/dev/null 2>&1; then
        pass "存在: lib/${name}.so.*"
    else
        fail "缺失: lib/${name}.so.*"
    fi
done
for name in librockchip_mpp librockchip_vpu; do
    if ls "$PKG_DIR/lib/${name}.so."* >/dev/null 2>&1; then
        pass "存在: lib/${name}.so.*"
    else
        fail "缺失: lib/${name}.so.*"
    fi
done
if [ -x "$PKG_DIR/test.sh" ]; then
    pass "存在且可执行: test.sh"
else
    fail "缺失或不可执行: test.sh"
fi
if [ -x "$PKG_DIR/network-e2e-test.sh" ]; then
    pass "存在且可执行: network-e2e-test.sh"
else
    fail "缺失或不可执行: network-e2e-test.sh"
fi
echo ""

# 2. 动态库依赖
echo "--- 动态库依赖 ---"
check_bundled_libs() {
    local bin="$1"
    local name="$2"
    local ldd_output="$3"

    for lib in librkvc libavcodec libavformat libavutil librockchip_mpp; do
        if echo "$ldd_output" | grep -q "$lib"; then
            if echo "$ldd_output" | grep "$lib" | grep -vq "$PKG_DIR/lib/"; then
                fail "$name: $lib 未解析到包内 lib/"
                echo "$ldd_output" | grep "$lib" | sed 's/^/  /'
            fi
        fi
    done
}

check_binary_deps() {
    local bin="$1"
    name="$(basename "$bin")"
    ldd_output=$(LD_LIBRARY_PATH="$PKG_DIR/lib" ldd "$bin" 2>&1 || true)
    not_found=$(echo "$ldd_output" | grep "not found" || true)
    if [ -z "$not_found" ]; then
        pass "$name: 所有依赖已满足"
    else
        fail "$name: 缺失依赖"
        echo "  $not_found"
        show_output "ldd $name" "$ldd_output"
    fi
    check_bundled_libs "$bin" "$name" "$ldd_output"
}

for bin in "$PKG_DIR/bin/"*; do
    [ -f "$bin" ] || continue
    check_binary_deps "$bin"
done
for bin in "$PKG_DIR/examples/bin/"*; do
    [ -f "$bin" ] || continue
    check_binary_deps "$bin"
done
echo ""

# 3. RPATH / RUNPATH
echo "--- RPATH / RUNPATH ---"
for bin in "$PKG_DIR/bin/"*; do
    [ -f "$bin" ] || continue
    check_runpath_contains "$bin" "bin/$(basename "$bin")" '$ORIGIN/../lib'
done
for bin in "$PKG_DIR/examples/bin/"*; do
    [ -f "$bin" ] || continue
    check_runpath_contains "$bin" "examples/bin/$(basename "$bin")" '$ORIGIN/../../lib'
done
for lib in "$PKG_DIR/lib/"*.so.*; do
    [ -f "$lib" ] || continue
    [ -L "$lib" ] && continue
    check_runpath_contains "$lib" "lib/$(basename "$lib")" '$ORIGIN'
done
echo ""

# 4. 功能测试
echo "--- 功能测试 ---"
capture_run norpath_status norpath_output env -u LD_LIBRARY_PATH "$PKG_DIR/bin/rkvc_info" --version
if [ "$norpath_status" -eq 0 ] && echo "$norpath_output" | grep -q "^rkvc "; then
    pass "无 LD_LIBRARY_PATH: rkvc_info --version"
else
    fail "无 LD_LIBRARY_PATH 运行失败 (exit=$norpath_status)"
    show_output "env -u LD_LIBRARY_PATH rkvc_info --version" "$norpath_output"
fi

capture_run ver_status ver_output env LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_info" --version
if [ "$ver_status" -eq 0 ] && echo "$ver_output" | grep -q "^rkvc "; then
    pass "rkvc_info --version: $ver_output"
else
    fail "rkvc_info --version 输出异常 (exit=$ver_status)"
    show_output "rkvc_info --version" "$ver_output"
fi

capture_run json_status json_output env LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_info" --json
if [ "$json_status" -eq 0 ] && echo "$json_output" | grep -q '"version"'; then
    pass "rkvc_info --json: 合法输出"
else
    fail "rkvc_info --json 输出异常 (exit=$json_status)"
    show_output "rkvc_info --json" "$json_output"
fi
for field in version rkmpp_enc rkmpp_dec dma_heap rga max_width max_height; do
    if echo "$json_output" | grep -q "\"$field\""; then
        pass "rkvc_info --json 字段: $field"
    else
        fail "rkvc_info --json 缺少字段: $field"
        show_output "rkvc_info --json" "$json_output"
    fi
done

echo ""
echo "--- 编解码测试 ---"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

capture_run enc_status enc_out env LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_encode" \
    --testsrc -o "$TMPDIR/test.h265" -s 640x480 -n 10 -v
if [ "$enc_status" -eq 0 ] && echo "$enc_out" | grep -q "编码完成"; then
    frames=$(extract_frames "$enc_out" "编码完成")
    pass "rkvc_encode: $frames"
else
    fail "rkvc_encode 编码失败 (exit=$enc_status)"
    echo "  命令: $PKG_DIR/bin/rkvc_encode --testsrc -o $TMPDIR/test.h265 -s 640x480 -n 10 -v"
    show_output "rkvc_encode" "$enc_out"
fi

if [ -f "$TMPDIR/test.h265" ]; then
    enc_size=$(file_size "$TMPDIR/test.h265")
    if [ "$enc_size" -gt 0 ]; then
        pass "编码产物非空: ${enc_size} bytes"
    else
        fail "编码产物为空: $TMPDIR/test.h265"
    fi

    if od -An -tx1 -N4 "$TMPDIR/test.h265" | tr -d ' \n' | grep -Eq '^(00000001|000001)'; then
        pass "编码产物包含 Annex-B start code"
    else
        fail "编码产物缺少 Annex-B start code"
        show_output "test.h265 前 16 字节" "$(od -An -tx1 -N16 "$TMPDIR/test.h265" 2>&1 || true)"
    fi

    capture_run dec_status dec_out env LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_decode" \
        -i "$TMPDIR/test.h265" -o "$TMPDIR/decoded.nv12"
    if [ "$dec_status" -eq 0 ] && echo "$dec_out" | grep -q "解码完成"; then
        frames=$(extract_frames "$dec_out" "解码完成")
        pass "rkvc_decode: $frames"
    else
        fail "rkvc_decode 解码失败 (exit=$dec_status)"
        echo "  命令: $PKG_DIR/bin/rkvc_decode -i $TMPDIR/test.h265 -o $TMPDIR/decoded.nv12"
        show_output "rkvc_decode" "$dec_out"
    fi

    if [ -f "$TMPDIR/decoded.nv12" ]; then
        dec_size=$(file_size "$TMPDIR/decoded.nv12")
        frame_size=$((640 * 480 * 3 / 2))
        if [ "$dec_size" -gt 0 ] && [ $((dec_size % frame_size)) -eq 0 ]; then
            pass "解码产物大小有效: ${dec_size} bytes"
        else
            fail "解码产物大小异常: ${dec_size} bytes (frame_size=$frame_size)"
        fi
    else
        fail "解码产物不存在: $TMPDIR/decoded.nv12"
    fi
else
    fail "跳过解码测试 (编码产物不存在)"
    echo "  期望文件不存在: $TMPDIR/test.h265"
    show_output "rkvc_encode" "$enc_out"
fi

pipe_file="$TMPDIR/pipe.nv12"
set +e
pipe_output=$(
    set -o pipefail
    LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_encode" --testsrc --stdout -s 640x480 -n 10 2>"$TMPDIR/pipe-enc.log" |
        LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_decode" --stdin --stdout -s 640x480 > "$pipe_file" 2>"$TMPDIR/pipe-dec.log"
)
pipe_status=$?
set -e
if [ "$pipe_status" -eq 0 ] && [ -f "$pipe_file" ]; then
    pipe_size=$(file_size "$pipe_file")
    pipe_frame_size=$((640 * 480 * 3 / 2))
    if [ "$pipe_size" -gt 0 ] && [ $((pipe_size % pipe_frame_size)) -eq 0 ]; then
        pass "管道模式输出大小有效: ${pipe_size} bytes"
    else
        fail "管道模式输出大小异常: ${pipe_size} bytes (frame_size=$pipe_frame_size)"
        show_output "pipe encode stderr" "$(cat "$TMPDIR/pipe-enc.log" 2>/dev/null || true)"
        show_output "pipe decode stderr" "$(cat "$TMPDIR/pipe-dec.log" 2>/dev/null || true)"
    fi
else
    fail "管道模式失败 (exit=$pipe_status)"
    show_output "pipe stdout" "$pipe_output"
    show_output "pipe encode stderr" "$(cat "$TMPDIR/pipe-enc.log" 2>/dev/null || true)"
    show_output "pipe decode stderr" "$(cat "$TMPDIR/pipe-dec.log" 2>/dev/null || true)"
fi

capture_run net_status net_out env LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/network-e2e-test.sh" \
    --mode udp --size 640x480 --frames 10 --bitrate 1000000 --timeout 30
if [ "$net_status" -eq 0 ] && echo "$net_out" | grep -q "网络端到端测试通过"; then
    pass "本机 UDP 网络端到端编解码回环"
else
    fail "本机 UDP 网络端到端编解码回环失败 (exit=$net_status)"
    show_output "network-e2e-test.sh" "$net_out"
fi

capture_run bench_status bench_out env LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_bench" \
    -s 640x480 -n 3 --encode-only -o "$TMPDIR/bench"
if [ "$bench_status" -eq 0 ] && echo "$bench_out" | grep -q "\[PASS\] encode"; then
    pass "rkvc_bench encode-only 短测"
else
    fail "rkvc_bench encode-only 短测失败 (exit=$bench_status)"
    echo "  命令: $PKG_DIR/bin/rkvc_bench -s 640x480 -n 3 --encode-only -o $TMPDIR/bench"
    show_output "rkvc_bench" "$bench_out"
fi

echo ""
echo "--- 开发文件 ---"
for h in rkvc.h encoder.h decoder.h stream.h frame.h types.h; do
    if [ -f "$PKG_DIR/include/rkvc/$h" ]; then
        pass "头文件: $h"
    else
        fail "缺失头文件: $h"
    fi
done

if [ -f "$PKG_DIR/share/pkgconfig/rkvc.pc" ]; then
    pass "pkg-config: rkvc.pc"
else
    fail "缺失: rkvc.pc"
fi

if command -v pkg-config >/dev/null 2>&1 && command -v cc >/dev/null 2>&1; then
    cat > "$TMPDIR/minimal.c" <<'EOF'
#include <rkvc/rkvc.h>
int main(void) { return rkvc_version() ? 0 : 1; }
EOF
    if PKG_CONFIG_PATH="$PKG_DIR/share/pkgconfig" \
       cc "$TMPDIR/minimal.c" -o "$TMPDIR/minimal" \
          $(PKG_CONFIG_PATH="$PKG_DIR/share/pkgconfig" pkg-config --cflags --libs rkvc) \
          -Wl,-rpath,"$PKG_DIR/lib" 2>"$TMPDIR/minimal-build.log" &&
       "$TMPDIR/minimal" >/dev/null 2>"$TMPDIR/minimal-run.log"; then
        pass "pkg-config 最小程序可编译运行"
    else
        fail "pkg-config 最小程序编译或运行失败"
        show_output "minimal build" "$(cat "$TMPDIR/minimal-build.log" 2>/dev/null || true)"
        show_output "minimal run" "$(cat "$TMPDIR/minimal-run.log" 2>/dev/null || true)"
    fi
else
    warn "跳过 pkg-config 最小编译测试 (缺少 pkg-config 或 cc)"
fi

echo ""
echo "--- 负向测试 ---"
expect_command_fail "rkvc_encode 缺少输出参数" "用法|OUTPUT" \
    env LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_encode" --testsrc -n 1 -s 640x480
expect_command_fail "rkvc_encode 缺少输入源" "需要 -i|--stdin|--testsrc|输入源" \
    env LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_encode" -o "$TMPDIR/unused.h265" -s 640x480
printf '\0\0\0\1\x67\xf4\0\x0a\x91\x96\x9e\xc0\x44\0\0\x03\0\x04\0\0\x03\0\x0a\x3c\x48\x9a\x80\0\0\0\1\x68\xce\x0f\x19\x20\0\0\1\x06' > "$TMPDIR/compressed-input.h264"
expect_command_fail "rkvc_encode 拒绝 H.264 压缩输入" "压缩码流|原始 NV12|rkvc_decode|transcode" \
    env LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_encode" -i "$TMPDIR/compressed-input.h264" -o "$TMPDIR/unused.h265" -s 640x480
expect_command_fail "rkvc_decode stdin 缺少分辨率" "stdin 模式需要" \
    env LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_decode" --stdin -o "$TMPDIR/unused.nv12"
expect_command_fail "rkvc_decode 输入文件不存在" "not found|No such file|codec or device not found" \
    env LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_decode" -i "$TMPDIR/missing.h265" -o "$TMPDIR/unused.nv12"

NEG_PKG="$TMPDIR/package-negative"
copy_package_tree "$NEG_PKG"
chmod -x "$NEG_PKG/bin/rkvc_encode"
expect_command_fail "负向包: rkvc_encode 不可执行" "Permission denied|权限不够|denied" \
    env LD_LIBRARY_PATH="$NEG_PKG/lib" "$NEG_PKG/bin/rkvc_encode" --version

copy_package_tree "$NEG_PKG"
rm -f "$NEG_PKG/lib/librockchip_mpp.so"*
neg_ldd=$(env -u LD_LIBRARY_PATH ldd "$NEG_PKG/bin/rkvc_info" 2>&1 || true)
neg_mpp_line=$(printf '%s\n' "$neg_ldd" | grep "librockchip_mpp" || true)
if [ -z "$neg_mpp_line" ]; then
    fail "负向包: ldd 未显示 librockchip_mpp 依赖"
    show_output "negative ldd" "$neg_ldd"
elif printf '%s\n' "$neg_mpp_line" | grep -Fq "$NEG_PKG/lib/"; then
    fail "负向包: 删除 librockchip_mpp 后仍解析到包内"
    show_output "negative ldd" "$neg_ldd"
else
    pass "负向包: 可检测 librockchip_mpp 缺失或串入系统库"
fi

if command -v patchelf >/dev/null 2>&1; then
    copy_package_tree "$NEG_PKG"
    patchelf --set-rpath "$NEG_PKG/lib" "$NEG_PKG/bin/rkvc_info"
    expect_runpath_check_fail "负向包: 可检测绝对 RPATH 注入" \
        "$NEG_PKG/bin/rkvc_info" "negative/bin/rkvc_info" '$ORIGIN/../lib'
else
    warn "跳过绝对 RPATH 注入负向测试 (缺少 patchelf)"
fi

# 总结
echo ""
echo "========================================="
echo -e "通过: ${GREEN}${PASS}${NC}  失败: ${RED}${FAIL}${NC}"
if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}所有测试通过!${NC}"
    exit 0
else
    echo -e "${RED}有测试失败!${NC}"
    exit 1
fi
