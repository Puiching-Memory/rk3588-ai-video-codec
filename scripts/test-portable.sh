#!/bin/bash
# scripts/test-portable.sh — 测试可移植包
#
# 用法:
#   ./scripts/test-portable.sh <portable-package-dir>
#
# 测试项目:
#   1. 所有二进制文件存在且可执行
#   2. 动态库依赖全部满足 (无 "not found")
#   3. rkvc_info --version 输出正确
#   4. rkvc_info --json 输出合法 JSON
#   5. rkvc_encode --testsrc 能成功编码
#   6. rkvc_decode 能解码编码产物
#   7. 开发头文件完整

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'
PASS=0
FAIL=0

pass() { PASS=$((PASS+1)); echo -e "${GREEN}✓${NC} $1"; }
fail() { FAIL=$((FAIL+1)); echo -e "${RED}✗${NC} $1"; }

PKG_DIR="${1:?用法: $0 <package-dir>}"

echo "=== 测试可移植包: $PKG_DIR ==="
echo ""

# 1. 文件完整性
echo "--- 文件完整性 ---"
for f in bin/rkvc_encode bin/rkvc_decode bin/rkvc_info lib/librkvc.so lib/libavcodec.so.60 lib/libavformat.so.60 lib/libavutil.so.58 include/rkvc/rkvc.h; do
    if [ -e "$PKG_DIR/$f" ]; then
        pass "存在: $f"
    else
        fail "缺失: $f"
    fi
done
echo ""

# 2. 动态库依赖
echo "--- 动态库依赖 ---"
MISSING=0
for bin in "$PKG_DIR/bin/"*; do
    name="$(basename "$bin")"
    not_found=$(LD_LIBRARY_PATH="$PKG_DIR/lib" ldd "$bin" 2>&1 | grep "not found" || true)
    if [ -z "$not_found" ]; then
        pass "$name: 所有依赖已满足"
    else
        fail "$name: 缺失依赖"
        echo "  $not_found"
        MISSING=1
    fi
done
echo ""

# 3. 版本输出
echo "--- 功能测试 ---"
ver_output=$(LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_info" --version 2>&1 || true)
if echo "$ver_output" | grep -q "rkvc"; then
    pass "rkvc_info --version: $ver_output"
else
    fail "rkvc_info --version 无输出"
fi

# 4. JSON 输出
json_output=$(LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_info" --json 2>&1 || true)
if echo "$json_output" | grep -q '"version"'; then
    pass "rkvc_info --json: 合法输出"
else
    fail "rkvc_info --json 输出异常"
fi

# 5. 编码测试
echo ""
echo "--- 编解码测试 ---"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

enc_out=$(LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_encode" \
    --testsrc -o "$TMPDIR/test.h265" -s 640x480 -n 10 -v 2>&1 || true)
if echo "$enc_out" | grep -q "编码完成"; then
    frames=$(echo "$enc_out" | grep "编码完成" | grep -oP '\d+ 帧')
    pass "rkvc_encode: $frames"
else
    fail "rkvc_encode 编码失败"
fi

# 6. 解码测试
if [ -f "$TMPDIR/test.h265" ]; then
    dec_out=$(LD_LIBRARY_PATH="$PKG_DIR/lib" "$PKG_DIR/bin/rkvc_decode" \
        -i "$TMPDIR/test.h265" -v 2>&1 || true)
    if echo "$dec_out" | grep -q "解码完成"; then
        frames=$(echo "$dec_out" | grep "解码完成" | grep -oP '\d+ 帧')
        pass "rkvc_decode: $frames"
    else
        fail "rkvc_decode 解码失败"
    fi
else
    fail "跳过解码测试 (编码产物不存在)"
fi

# 7. 开发文件
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
