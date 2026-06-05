#!/bin/bash
# network-e2e-test.sh - 本机网络端到端编解码回环测试
#
# 流程:
#   1. rkvc_encode --testsrc 生成短 H.265 输入
#   2. example_stream_device_pair 在 127.0.0.1 上模拟发送端/接收端
#   3. 接收端解码并校验发送/接收帧数一致

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

usage() {
    cat <<'EOF'
用法:
  ./network-e2e-test.sh [选项]
  ./network-e2e-test.sh <portable-package-dir> [选项]

选项:
  -m, --mode udp|rtp|both   网络模式 (默认 udp)
  -s, --size WxH            测试分辨率 (默认 640x480)
  -n, --frames N            测试帧数 (默认 10)
  -b, --bitrate BPS         传输码率 (默认 1000000)
  -p, --port N              本机 UDP/RTP 端口 (默认 19000 + PID)
  -t, --timeout SEC         单步超时时间 (默认 30)
  --keep-tmp                保留临时目录
  -h, --help                显示帮助

示例:
  ./network-e2e-test.sh
  ./network-e2e-test.sh --mode rtp --size 1280x720 --frames 30
EOF
}

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

capture_command() {
    local __status_var="$1"
    local __out_var="$2"
    shift 2

    local cmd_output cmd_status
    set +e
    cmd_output=$("$@" 2>&1)
    cmd_status=$?
    set -e
    printf -v "$__status_var" '%s' "$cmd_status"
    printf -v "$__out_var" '%s' "$cmd_output"
}

file_size() {
    stat -c '%s' "$1" 2>/dev/null || wc -c < "$1"
}

run_with_timeout() {
    local __status_var="$1"
    local __out_var="$2"
    shift 2

    if command -v timeout >/dev/null 2>&1; then
        capture_command "$__status_var" "$__out_var" timeout "$TIMEOUT" "$@"
    else
        warn "系统缺少 timeout，命令不会自动超时"
        capture_command "$__status_var" "$__out_var" "$@"
    fi
}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

MODE="${RKVC_NET_MODE:-udp}"
SIZE="${RKVC_NET_SIZE:-640x480}"
FRAMES="${RKVC_NET_FRAMES:-10}"
BITRATE="${RKVC_NET_BITRATE:-1000000}"
PORT="${RKVC_NET_PORT:-$((19000 + ($$ % 10000)))}"
TIMEOUT="${RKVC_NET_TIMEOUT:-30}"
KEEP_TMP="${RKVC_NET_KEEP_TMP:-0}"

if [[ $# -gt 0 && -d "$1" && -x "$1/bin/rkvc_encode" ]]; then
    PKG_DIR="$(cd "$1" && pwd)"
    shift
elif [[ -x "$SCRIPT_DIR/bin/rkvc_encode" && -d "$SCRIPT_DIR/lib" ]]; then
    PKG_DIR="$SCRIPT_DIR"
else
    echo "错误: 未找到可移植包目录"
    usage
    exit 2
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        -m|--mode)
            if [[ -z "${2:-}" ]]; then echo "错误: $1 需要参数"; exit 2; fi
            MODE="$2"; shift 2 ;;
        -s|--size)
            if [[ -z "${2:-}" ]]; then echo "错误: $1 需要参数"; exit 2; fi
            SIZE="$2"; shift 2 ;;
        -n|--frames)
            if [[ -z "${2:-}" ]]; then echo "错误: $1 需要参数"; exit 2; fi
            FRAMES="$2"; shift 2 ;;
        -b|--bitrate)
            if [[ -z "${2:-}" ]]; then echo "错误: $1 需要参数"; exit 2; fi
            BITRATE="$2"; shift 2 ;;
        -p|--port)
            if [[ -z "${2:-}" ]]; then echo "错误: $1 需要参数"; exit 2; fi
            PORT="$2"; shift 2 ;;
        -t|--timeout)
            if [[ -z "${2:-}" ]]; then echo "错误: $1 需要参数"; exit 2; fi
            TIMEOUT="$2"; shift 2 ;;
        --keep-tmp)
            KEEP_TMP=1; shift ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "错误: 未知参数 $1"
            usage
            exit 2 ;;
    esac
done

case "$MODE" in
    udp|rtp|both) ;;
    *) echo "错误: --mode 仅支持 udp、rtp 或 both"; exit 2 ;;
esac

if [[ ! "$SIZE" =~ ^[0-9]+x[0-9]+$ ]]; then
    echo "错误: --size 必须是 WxH，例如 640x480"
    exit 2
fi
for value_name in FRAMES BITRATE PORT TIMEOUT; do
    value="${!value_name}"
    if [[ ! "$value" =~ ^[0-9]+$ || "$value" -le 0 ]]; then
        echo "错误: $value_name 必须是正整数"
        exit 2
    fi
done

ENCODER="$PKG_DIR/bin/rkvc_encode"
PAIR="$PKG_DIR/examples/bin/example_stream_device_pair"

if [ ! -x "$ENCODER" ]; then
    echo "错误: 缺少可执行文件 $ENCODER"
    exit 2
fi
if [ ! -x "$PAIR" ]; then
    echo "错误: 缺少可执行文件 $PAIR"
    exit 2
fi

RUN_ENV=(env "LD_LIBRARY_PATH=$PKG_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}")
TMP_ROOT="${TMPDIR:-/tmp}"
WORK_DIR="$(mktemp -d "$TMP_ROOT/rkvc-net-e2e.XXXXXX")"
INPUT="$WORK_DIR/input.h265"

cleanup() {
    if [[ "$KEEP_TMP" == "1" ]]; then
        echo "保留临时目录: $WORK_DIR"
    else
        rm -rf "$WORK_DIR"
    fi
}
trap cleanup EXIT

echo "=== 本机网络端到端编解码测试 ==="
echo "包目录: $PKG_DIR"
echo "模式:   $MODE"
echo "分辨率: $SIZE"
echo "帧数:   $FRAMES"
echo "端口:   $PORT"
echo ""

echo "--- 生成测试 H.265 输入 ---"
run_with_timeout enc_status enc_out "${RUN_ENV[@]}" "$ENCODER" \
    --testsrc -o "$INPUT" -s "$SIZE" -n "$FRAMES" -b "$BITRATE" -v
if [ "$enc_status" -eq 0 ] && [ -f "$INPUT" ] && [ "$(file_size "$INPUT")" -gt 0 ]; then
    pass "测试输入生成成功: $(file_size "$INPUT") bytes"
else
    fail "测试输入生成失败 (exit=$enc_status)"
    show_output "rkvc_encode" "$enc_out"
fi

run_loopback() {
    local mode="$1"
    local port="$2"
    local status output sent="" recv=""

    echo ""
    echo "--- 本机 ${mode^^} 网络回环 ---"
    run_with_timeout status output "${RUN_ENV[@]}" "$PAIR" \
        -i "$INPUT" -c "$mode" -r both \
        --dst-ip 127.0.0.1 --dst-port "$port" --bind-port "$port" \
        -s "$SIZE" -b "$BITRATE"

    show_output "example_stream_device_pair ($mode)" "$output"

    if [ "$status" -ne 0 ]; then
        fail "$mode 回环命令失败 (exit=$status)"
        return
    fi

    if [[ "$output" =~ 发送端:[[:space:]]*([0-9]+)[[:space:]]*帧 ]]; then
        sent="${BASH_REMATCH[1]}"
    fi
    if [[ "$output" =~ 接收端:[[:space:]]*([0-9]+)[[:space:]]*帧 ]]; then
        recv="${BASH_REMATCH[1]}"
    fi

    if [[ -n "$recv" && "$recv" -gt 0 ]]; then
        pass "$mode 接收端解码 ${recv} 帧"
    else
        fail "$mode 接收端未解码到有效帧"
    fi

    if [[ -n "$sent" && -n "$recv" && "$sent" -eq "$recv" && "$recv" -gt 0 ]]; then
        pass "$mode 发送/接收帧数一致: ${sent} 帧"
    else
        fail "$mode 发送/接收帧数不一致: send=${sent:-未知}, recv=${recv:-未知}"
    fi

    if echo "$output" | grep -Eq '帧完整:[[:space:]]*是'; then
        pass "$mode 端到端帧完整"
    else
        fail "$mode 端到端帧完整性检查未通过"
    fi
}

if [ "$FAIL" -eq 0 ]; then
    case "$MODE" in
        udp)
            run_loopback udp "$PORT" ;;
        rtp)
            run_loopback rtp "$PORT" ;;
        both)
            run_loopback udp "$PORT"
            run_loopback rtp "$((PORT + 1))" ;;
    esac
else
    warn "跳过网络回环测试 (测试输入生成失败)"
fi

echo ""
echo "========================================="
echo -e "通过: ${GREEN}${PASS}${NC}  失败: ${RED}${FAIL}${NC}"
if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}网络端到端测试通过${NC}"
    exit 0
else
    echo -e "${RED}网络端到端测试失败${NC}"
    exit 1
fi
