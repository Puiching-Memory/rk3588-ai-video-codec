#!/usr/bin/env bash
# bench/run_rd_benchmark.sh — RK3588 端到端 RD 基准（集成于 rkvc 项目）
#
# 对比路线（默认）: h264 / h265 / svt-av1 / rkvc-v2 / post-upscale
# 实验路线（搁置）: svt-av1+superres — AV1 内建 superres，见 bench/README.md
#
# 用法:
#   ./scripts/run-bench.sh [源视频.mp4]
#   RUN_CODECS=h264,rkvc-v2 ./scripts/run-bench.sh clip.mp4
#   PLOT_ONLY=1 ./scripts/run-bench.sh

set -euo pipefail

BENCH_ROOT="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$BENCH_ROOT/.." && pwd)"
RESULTS="$BENCH_ROOT/results"
WORKDIR="$BENCH_ROOT/work"
RAMDISK_DIR="${RAMDISK_DIR:-/dev/shm/rkvc-bench}"
RAM_WORK_DIR="$RAMDISK_DIR/work"

# shellcheck source=../scripts/build-common.sh
source "$PROJECT_ROOT/scripts/build-common.sh" 2>/dev/null || true

SRC_VIDEO="${1:-${SRC_VIDEO:-}}"
CLIP_SEC="${CLIP_SEC:-4}"
# 片段起点：middle=从源视频时长居中截取；start=从 0s 起（可用 CLIP_START_SEC 显式指定秒数）
CLIP_OFFSET="${CLIP_OFFSET:-middle}"
CLIP_START_SEC="${CLIP_START_SEC:-}"
# 目标码率点（kbps）：低端加密以显现上采样优势区，高端覆盖至 1000
TARGET_KBPS="${TARGET_KBPS:-25,30,40,50,60,80,100,150,200,300,400,500,600,700,800,900,1000}"
IFS=',' read -ra BITRATES <<< "$TARGET_KBPS"
SVT_PRESET="${SVT_PRESET:-11}"
# SVT RD 扫点模式：calibrated=CRF/CQP 校准表（actual≈target）；vbr=旧版 --rc 1 --tbr
SVT_RD_MODE="${SVT_RD_MODE:-calibrated}"
# AV1 内建 superres（SVT-AV1）：mode 0=off 1=fixed 2=random 3=qthresh 4=auto
SVT_SUPERRES_MODE="${SVT_SUPERRES_MODE:-4}"
SVT_SUPERRES_DENOM="${SVT_SUPERRES_DENOM:-9}"
SVT_SUPERRES_KF_DENOM="${SVT_SUPERRES_KF_DENOM:-$SVT_SUPERRES_DENOM}"
SVT_SUPERRES_QTHRES="${SVT_SUPERRES_QTHRES:-48}"
SVT_SUPERRES_KF_QTHRES="${SVT_SUPERRES_KF_QTHRES:-$SVT_SUPERRES_QTHRES}"
# superres 码流 av1_rkmpp 硬解会崩溃，默认用系统 ffmpeg + libaom-av1 软解
SVT_SUPERRES_FFMPEG="${SVT_SUPERRES_FFMPEG:-/usr/bin/ffmpeg}"
ENC_SCALE_DENOM="${ENC_SCALE_DENOM:-2}"
UPSCALE_ALGOS="${UPSCALE_ALGOS:-nearest,bilinear,bicubic}"
RUN_CODECS="${RUN_CODECS:-h264,h265,svt-av1,rkvc-v2}"
RKVC_POLICIES="${RKVC_POLICIES:-realtime,balanced,quality}"
RKVC_BUILD="${RKVC_BUILD:-${RKVC_BUILD_DIR:-$PROJECT_ROOT/build}}"
case "$RKVC_BUILD" in /*) ;; *) RKVC_BUILD="$PROJECT_ROOT/$RKVC_BUILD" ;; esac

MPP_LIB="$PROJECT_ROOT/build-deps/mpp-install/lib"
FFMPEG_SRC="$PROJECT_ROOT/third_party/ffmpeg-rockchip"
SVT_PREFIX="$PROJECT_ROOT/build-deps/svt-av1-install"

resolve_ffmpeg() {
    if [[ -n "${FFMPEG:-}" && -x "$FFMPEG" ]]; then
        echo "$FFMPEG"
        return
    fi
    if [[ -x "$FFMPEG_SRC/ffmpeg" ]]; then
        echo "$FFMPEG_SRC/ffmpeg"
        return
    fi
    if command -v ffmpeg >/dev/null 2>&1; then
        command -v ffmpeg
        return
    fi
    echo "[error] 未找到 ffmpeg，请先运行 ./scripts/rebuild-ffmpeg-rkmpp.sh" >&2
    exit 1
}

FFMPEG="$(resolve_ffmpeg)"
# 默认使用自包含 ffmpeg-rockchip；可用 PREP_FFMPEG 覆盖
PREP_FFMPEG="${PREP_FFMPEG:-$FFMPEG}"
FFPROBE="${FFPROBE:-}"
if [[ -z "$FFPROBE" ]]; then
    if [[ -x "${FFMPEG%/*}/ffprobe" ]]; then
        FFPROBE="${FFMPEG%/*}/ffprobe"
    elif command -v /usr/bin/ffprobe >/dev/null 2>&1; then
        FFPROBE=/usr/bin/ffprobe
    else
        FFPROBE="$(command -v ffprobe || true)"
    fi
fi
SVT_ENC="${SVT_ENC:-$SVT_PREFIX/bin/SvtAv1EncApp}"
RKVC_TRANS="$RKVC_BUILD/rkvc_transcode"
RKVC_YUV_UPSCALE="$RKVC_BUILD/rkvc_yuv_upscale"
RKVC_SESSION_UPSCALE="$RKVC_BUILD/rkvc_session_upscale"
RKVC_ENC="$RKVC_BUILD/rkvc_encode"

FFMPEG_LIB_DIRS=""
for _d in "$FFMPEG_SRC"/libav* "$FFMPEG_SRC"/libsw* "$FFMPEG_SRC"/libpostproc; do
    [[ -d "$_d" ]] && FFMPEG_LIB_DIRS="${_d}:${FFMPEG_LIB_DIRS}"
done
export LD_LIBRARY_PATH="$MPP_LIB:${FFMPEG_LIB_DIRS}$SVT_PREFIX/lib:${RKVC_BUILD}:${LD_LIBRARY_PATH:-}"
export PATH="$SVT_PREFIX/bin:${FFMPEG%/*}:$PATH"

mkdir -p "$RESULTS" "$WORKDIR" "$RAMDISK_DIR" "$RAM_WORK_DIR"

CLIP_MP4="$RAMDISK_DIR/clip.mp4"
REF_Y4M_RAM="$RAMDISK_DIR/clip.y4m"
REF_YUV_RAM="$RAMDISK_DIR/clip.yuv"
REF_NV12_RAM="$RAMDISK_DIR/clip.nv12"
CLIP_META="$WORKDIR/clip.meta"
CSV="$RESULTS/rd_data.csv"
CSV_HEADER="codec,target_kbps,actual_kbps,psnr_y,psnr_u,psnr_v,psnr_avg,ssim,encode_sec,decode_sec,rga_sec,write_sec,postproc_sec"
CSV_FIELDS="codec,target_kbps,actual_kbps,psnr_y,psnr_u,psnr_v,psnr_avg,ssim,encode_sec,decode_sec,rga_sec,write_sec,postproc_sec"

# 码流/YUV 走 tmpfs；日志与 stats 留在 work/ 便于排查。
bench_ramdir() {
    echo "$RAM_WORK_DIR/$1/$2"
}

bench_logdir() {
    echo "$WORKDIR/logs/$1/$2"
}

# 测完即删大体积 YUV，避免 tmpfs 被撑满（单帧 1080p YUV ~3MB × 62 帧 × 多路线）。
bench_cleanup_ramdir() {
    local ramdir="$1"
    rm -f "$ramdir"/decoded.yuv "$ramdir"/decoded.nv12 \
        "$ramdir"/decoded_lo.yuv "$ramdir"/decoded_lo.nv12 \
        "$ramdir"/upscaled.yuv "$ramdir"/upscaled.nv12 \
        "$ramdir"/stream.mp4 "$ramdir"/stream.ivf
}

usage() {
    cat <<EOF
用法: $(basename "$0") [源视频.mp4]

环境变量:
  SRC_VIDEO      源视频路径（也可作为第一个参数）
  RUN_CODECS     默认 h264,h265,svt-av1,rkvc-v2（rkvc-v2 展开为三档 policy）
  ENC_SCALE_DENOM  下采样编码分母（post-upscale 路线，默认 2）
  UPSCALE_ALGOS  后处理上采样算法列表（RGA 硬件，默认 nearest,bilinear,bicubic）
  RKVC_POLICIES  rkvc Session 语义档位（默认 realtime,balanced,quality）
  CLIP_SEC       截取秒数（默认 4）
  CLIP_OFFSET    截取位置：middle（默认，居中）| start（从头）
  CLIP_START_SEC 显式起点秒数（设置后覆盖 CLIP_OFFSET）
  TARGET_KBPS    目标码率点，逗号分隔 kbps（默认 50,100,150,200,300,400,500,600,700,800,900,1000）
  SVT_RD_MODE    SVT-AV1 RD 扫点：calibrated（默认，CRF/CQP 表）或 vbr（--rc 1 --tbr）
  SVT_SUPERRES_MODE   AV1 内建 superres 模式（默认 4=auto；1=fixed 3=qthresh）
  SVT_SUPERRES_DENOM  superres 分母 9~16（默认 9，即水平 8/9；16=水平 1/2）
  SVT_SUPERRES_QTHRES superres QP 阈值（mode=3 时，默认 48）
  SVT_SUPERRES_FFMPEG  superres 解码用 ffmpeg（默认 /usr/bin/ffmpeg libaom-av1 软解；av1_rkmpp 暂不可用）
  RKVC_BUILD     rkvc 构建目录（默认 $PROJECT_ROOT/build）
  PLOT_ONLY=1    仅根据已有 CSV 绘图
  RAMDISK_DIR    中间帧/码流缓存（默认 /dev/shm/rkvc-bench，不落盘 eMMC）

前置条件:
  ./scripts/build-svt.sh
  ./scripts/rebuild-ffmpeg-rkmpp.sh
  cmake -B build && cmake --build build -j4   # rkvc_transcode
EOF
}

[[ "${1:-}" == "-h" || "${1:-}" == "--help" ]] && { usage; exit 0; }

plot_results() {
    local frames="${1:-62}"
    local title_rd title_perf
    if [[ -n "${WIDTH:-}" && "${WIDTH}" -gt 0 ]]; then
        enc_dims
        title_rd="E2E RD Curve (${CLIP_SEC}s@${CLIP_START}s, ${WIDTH}x${HEIGHT} vs ${ENC_W}x${ENC_H} + ${ENC_SCALE_DENOM}x upscale)"
        title_perf="E2E Performance (${CLIP_SEC}s@${CLIP_START}s, 1080p vs ${ENC_H}p+${ENC_SCALE_DENOM}x up)"
    else
        title_rd="E2E RD Curve (${CLIP_SEC}s@${CLIP_START:-0}s, post-upscale ${ENC_SCALE_DENOM}x)"
        title_perf="E2E Performance (${CLIP_SEC}s@${CLIP_START:-0}s, post-upscale ${ENC_SCALE_DENOM}x)"
    fi

    if [[ -f "$BENCH_ROOT/.venv/bin/python" ]]; then
        (cd "$BENCH_ROOT" && .venv/bin/python plot_rd_curve.py --csv "$CSV" \
            --out "$RESULTS/rd_curve_e2e" --title "$title_rd")
        (cd "$BENCH_ROOT" && .venv/bin/python plot_perf.py --csv "$CSV" \
            --out "$RESULTS/perf_e2e" --frames "$frames" --title "$title_perf")
        return
    fi
    if command -v uv >/dev/null 2>&1; then
        (cd "$BENCH_ROOT" && uv run plot_rd_curve.py --csv "$CSV" \
            --out "$RESULTS/rd_curve_e2e" --title "$title_rd")
        (cd "$BENCH_ROOT" && uv run plot_perf.py --csv "$CSV" \
            --out "$RESULTS/perf_e2e" --frames "$frames" --title "$title_perf")
        return
    fi
    if python3 -c "import matplotlib" 2>/dev/null; then
        (cd "$BENCH_ROOT" && python3 plot_rd_curve.py --csv "$CSV" \
            --out "$RESULTS/rd_curve_e2e" --title "$title_rd")
        (cd "$BENCH_ROOT" && python3 plot_perf.py --csv "$CSV" \
            --out "$RESULTS/perf_e2e" --frames "$frames" --title "$title_perf")
        return
    fi
    echo "[warn] 未安装 matplotlib，跳过绘图。可在 bench/ 下运行: uv sync && uv run plot_rd_curve.py"
}

if [[ "${PLOT_ONLY:-0}" == "1" ]]; then
    plot_results "${FRAMES:-62}"
    exit 0
fi

if [[ -z "$SRC_VIDEO" ]]; then
    echo "[error] 请指定源视频: $0 /path/to/video.mp4 或设置 SRC_VIDEO" >&2
    usage
    exit 1
fi
if [[ ! -f "$SRC_VIDEO" ]]; then
    echo "[error] 源视频不存在: $SRC_VIDEO" >&2
    exit 1
fi

sync_ref_to_ram() {
    : # 参考帧已在 RAMDISK_DIR 生成，无需再同步
}

clip_meta_key() {
    local mtime
    mtime=$(stat -c %Y "$SRC_VIDEO" 2>/dev/null || echo 0)
    echo "${CLIP_SEC}|${CLIP_OFFSET}|${CLIP_START}|${SRC_VIDEO}|${mtime}"
}

compute_clip_start() {
    if [[ -n "$CLIP_START_SEC" ]]; then
        CLIP_START="$CLIP_START_SEC"
        return
    fi
    if [[ "$CLIP_OFFSET" == "start" ]]; then
        CLIP_START=0
        return
    fi
    local dur
    dur=$(probe_src_duration_sec)
    CLIP_START=$(python3 - "$dur" "$CLIP_SEC" <<'PY'
import sys
dur = float(sys.argv[1] or 0)
clip = float(sys.argv[2])
if dur <= clip:
    print("0")
else:
    print(f"{(dur - clip) / 2:.3f}")
PY
)
}

src_is_raw_elementary() {
    case "${SRC_VIDEO##*.}" in
        h265|hevc|265|h264|264|avc) return 0 ;;
        *) return 1 ;;
    esac
}

prep_input_format_args() {
    case "${SRC_VIDEO##*.}" in
        h265|hevc|265) echo "-f hevc" ;;
        h264|264|avc)  echo "-f h264" ;;
        *) echo "" ;;
    esac
}

# 裸 .h265/.h264 需系统 ffmpeg demux；容器格式仍用 PREP_FFMPEG（rockchip 构建）。
resolve_demux_ffmpeg() {
    if src_is_raw_elementary && [[ -x /usr/bin/ffmpeg ]]; then
        echo /usr/bin/ffmpeg
        return
    fi
    echo "$PREP_FFMPEG"
}

resolve_demux_ffprobe() {
    if src_is_raw_elementary && [[ -x /usr/bin/ffprobe ]]; then
        echo /usr/bin/ffprobe
        return
    fi
    echo "$FFPROBE"
}

# 源视频时长（秒）；裸码流首次 count_frames 较慢，结果缓存在 work/。
probe_src_duration_sec() {
    local cache_key
    cache_key=$(python3 -c "import hashlib,sys; print(hashlib.sha256(sys.argv[1].encode()).hexdigest()[:16])" "$SRC_VIDEO")
    local cache="$WORKDIR/src_duration_${cache_key}.cache"
    local mtime cached_mtime cached_dur
    mtime=$(stat -c %Y "$SRC_VIDEO" 2>/dev/null || echo 0)
    if [[ -f "$cache" ]]; then
        read -r cached_mtime cached_dur < "$cache" || true
        if [[ "$cached_mtime" == "$mtime" && -n "$cached_dur" ]]; then
            echo "$cached_dur"
            return
        fi
    fi

    local demux_probe dur
    local -a prep_in
    demux_probe=$(resolve_demux_ffprobe)
    prep_in=()
    read -ra prep_in <<< "$(prep_input_format_args)"
    dur=$("$demux_probe" -v error "${prep_in[@]}" -show_entries format=duration -of csv=p=0 \
        "$SRC_VIDEO" 2>/dev/null || true)

    if [[ -z "$dur" || "$dur" == "N/A" ]]; then
        if src_is_raw_elementary; then
            echo "[prep] 裸码流无 duration，count_frames 探测时长（首次较慢，已缓存）..." >&2
            local probe_out fps_s frames
            probe_out=$("$demux_probe" -v error "${prep_in[@]}" -select_streams v:0 -count_frames \
                -show_entries stream=nb_read_frames,r_frame_rate -of csv=p=0 "$SRC_VIDEO" 2>/dev/null || true)
            fps_s=$(echo "$probe_out" | cut -d, -f1)
            frames=$(echo "$probe_out" | cut -d, -f2)
            dur=$(FPS_S="$fps_s" FRAMES_N="$frames" python3 - <<'PY'
import os
fps_s = os.environ.get("FPS_S", "30/1")
n = int(os.environ.get("FRAMES_N", "0") or 0)
if "/" in fps_s:
    a, b = fps_s.split("/", 1)
    fps = float(a) / float(b) if float(b) else 30.0
else:
    fps = float(fps_s) if fps_s else 30.0
print(n / fps if fps > 0 and n > 0 else 0)
PY
)
        else
            dur=0
        fi
    fi

    echo "$mtime $dur" > "$cache"
    echo "$dur"
}

prepare_clip() {
    compute_clip_start
    local meta_key
    meta_key=$(clip_meta_key)
    if [[ ! -f "$CLIP_META" ]] || [[ "$(cat "$CLIP_META")" != "$meta_key" ]] \
        || [[ ! -f "$CLIP_MP4" ]] || [[ ! -f "$REF_YUV_RAM" ]] \
        || [[ ! -f "$REF_NV12_RAM" ]]; then
        echo "[prep] 从 ${CLIP_START}s 截取 ${CLIP_SEC}s（offset=${CLIP_OFFSET}）→ ${RAMDISK_DIR} ..."
        rm -f "$CLIP_MP4" "$REF_Y4M_RAM" "$REF_YUV_RAM" "$REF_NV12_RAM"
        echo "$meta_key" > "$CLIP_META"

        local demux_ff demux_probe
        local -a prep_in
        demux_ff=$(resolve_demux_ffmpeg)
        demux_probe=$(resolve_demux_ffprobe)
        prep_in=()
        read -ra prep_in <<< "$(prep_input_format_args)"

        if src_is_raw_elementary; then
            echo "[prep] 裸码流输入，使用 ${demux_ff} ${prep_in[*]} 解码 ..."
            # 裸码流须在 -i 之后 -ss（input seek 不可靠）。
            "$demux_ff" -y "${prep_in[@]}" -i "$SRC_VIDEO" -ss "$CLIP_START" -t "$CLIP_SEC" \
                -pix_fmt yuv420p -f yuv4mpegpipe "$REF_Y4M_RAM" 2>/dev/null
            "$demux_ff" -y "${prep_in[@]}" -i "$SRC_VIDEO" -ss "$CLIP_START" -t "$CLIP_SEC" \
                -pix_fmt yuv420p -f rawvideo "$REF_YUV_RAM" 2>/dev/null
            "$demux_ff" -y "${prep_in[@]}" -i "$SRC_VIDEO" -ss "$CLIP_START" -t "$CLIP_SEC" \
                -pix_fmt nv12 -f rawvideo "$REF_NV12_RAM" 2>/dev/null

            local _w _h _fps
            _w=$("$demux_probe" -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 "$REF_Y4M_RAM")
            _h=$("$demux_probe" -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$REF_Y4M_RAM")
            _fps=$("$demux_probe" -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$REF_Y4M_RAM")
            # clip.mp4 仅供 ffprobe / rkvc_transcode；由已解码 YUV 封装，避免二次有损。
            "$demux_ff" -y -f rawvideo -pix_fmt yuv420p -video_size "${_w}x${_h}" \
                -framerate "$_fps" -i "$REF_YUV_RAM" -c:v libx264 -crf 10 -preset ultrafast -an \
                "$CLIP_MP4" 2>/dev/null
        else
            "$PREP_FFMPEG" -y -ss "$CLIP_START" -t "$CLIP_SEC" -i "$SRC_VIDEO" -c copy -an "$CLIP_MP4" 2>/dev/null
            "$PREP_FFMPEG" -y -i "$CLIP_MP4" -pix_fmt yuv420p -f yuv4mpegpipe "$REF_Y4M_RAM" 2>/dev/null
            "$PREP_FFMPEG" -y -i "$CLIP_MP4" -pix_fmt yuv420p -f rawvideo "$REF_YUV_RAM" 2>/dev/null
            "$PREP_FFMPEG" -y -i "$CLIP_MP4" -pix_fmt nv12 -f rawvideo "$REF_NV12_RAM" 2>/dev/null
        fi
    fi
}

prepare_clip
sync_ref_to_ram

SRC_CLIP="$CLIP_MP4"
WIDTH=$("$FFPROBE" -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 "$SRC_CLIP")
HEIGHT=$("$FFPROBE" -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$SRC_CLIP")
FPS_NUM=$("$FFPROBE" -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$SRC_CLIP" | awk -F/ '{print $1}')
FPS_DEN=$("$FFPROBE" -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$SRC_CLIP" | awk -F/ '{if (NF>1) print $2; else print 1}')
FRAMES=$("$FFPROBE" -v error -select_streams v:0 -count_frames -show_entries stream=nb_read_frames -of csv=p=0 "$SRC_CLIP")
DURATION=$("$FFPROBE" -v error -show_entries format=duration -of csv=p=0 "$SRC_CLIP")

if [[ ! -f "$CSV" ]]; then
    echo "$CSV_HEADER" > "$CSV"
elif ! head -1 "$CSV" | grep -q rga_sec; then
    python3 - "$CSV" <<'PY'
import csv, sys
path = sys.argv[1]
fields = [
    "codec", "target_kbps", "actual_kbps", "psnr_y", "psnr_u", "psnr_v",
    "psnr_avg", "ssim", "encode_sec", "decode_sec", "rga_sec", "write_sec",
    "postproc_sec",
]
rows = list(csv.DictReader(open(path, newline="")))
with open(path, "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=fields)
    w.writeheader()
    for r in rows:
        r.setdefault("postproc_sec", "0.0")
        post = float(r.get("postproc_sec") or 0)
        r.setdefault("rga_sec", f"{post:.3f}" if post else "0.0")
        r.setdefault("write_sec", "0.0")
        w.writerow({k: r.get(k, "") for k in fields})
PY
fi

# 本次跑出的分片 CSV 合并进 rd_data.csv：同 codec 整批替换，非追加重复行。
finalize_csv() {
    python3 - "$CSV" "$WORKDIR" <<'PY'
import csv, sys
from pathlib import Path

main_path = Path(sys.argv[1])
workdir = Path(sys.argv[2])
fields = [
    "codec", "target_kbps", "actual_kbps", "psnr_y", "psnr_u", "psnr_v",
    "psnr_avg", "ssim", "encode_sec", "decode_sec", "rga_sec", "write_sec",
    "postproc_sec",
]

def _norm_row(row):
    row.setdefault("postproc_sec", "0.0")
    row.setdefault("rga_sec", "0.0")
    row.setdefault("write_sec", "0.0")
    post = float(row.get("postproc_sec") or 0)
    rga = float(row.get("rga_sec") or 0)
    wr = float(row.get("write_sec") or 0)
    if post <= 0 and (rga > 0 or wr > 0):
        post = rga + wr
    elif rga <= 0 and wr <= 0 and post > 0:
        row["rga_sec"] = f"{post:.3f}"
    row["postproc_sec"] = f"{post:.3f}"
    row["rga_sec"] = f"{rga:.3f}"
    row["write_sec"] = f"{wr:.3f}"
    return {k: row.get(k, "") for k in fields}

new_rows = []
rerun = set()
for partial in sorted(workdir.glob("results_*.csv")):
    with partial.open(newline="") as f:
        for row in csv.DictReader(f):
            new_rows.append(_norm_row(row))
            rerun.add(row["codec"])

preserved = []
if main_path.is_file() and main_path.stat().st_size > 0:
    with main_path.open(newline="") as f:
        for row in csv.DictReader(f):
            if row["codec"] not in rerun:
                preserved.append(_norm_row(row))

tmp = main_path.with_suffix(".csv.tmp")
with tmp.open("w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=fields)
    w.writeheader()
    w.writerows(preserved)
    w.writerows(new_rows)
tmp.replace(main_path)
PY
}

parse_quality_log() {
    python3 - "$1" <<'PY'
import re, sys
log = open(sys.argv[1]).read()
psnr_m = re.findall(r"PSNR y:([\d.]+) u:([\d.]+) v:([\d.]+) average:([\d.]+)", log)
ssim_m = re.findall(r"All:([\d.]+)", log.split("Parsed_ssim")[-1] if "Parsed_ssim" in log else log)
if not psnr_m or not ssim_m:
    raise SystemExit(f"quality parse failed: {sys.argv[1]}")
psnr = psnr_m[-1]
ssim = ssim_m[-1]
print(",".join((*psnr, ssim)))
PY
}

measure_quality() {
    local decoded="$1" stats="$2"
    "$PREP_FFMPEG" -y -i "$SRC_CLIP" -i "$decoded" -shortest \
        -lavfi "[0:v][1:v]psnr=stats_file=${stats}.psnr;[0:v][1:v]ssim=stats_file=${stats}.ssim" \
        -f null /dev/null 2>"${stats}.log"
    parse_quality_log "${stats}.log"
}

measure_quality_yuv() {
    local decoded_yuv="$1" ref_yuv="$2" frames="$3" stats="$4"
    local fps_num="$5" fps_den="$6"
    local fps=$((fps_num / fps_den))
    "$PREP_FFMPEG" -y -f rawvideo -pix_fmt yuv420p -s "${WIDTH}x${HEIGHT}" -r "$fps" -i "$decoded_yuv" \
        -f rawvideo -pix_fmt yuv420p -s "${WIDTH}x${HEIGHT}" -r "$fps" -i "$ref_yuv" \
        -frames:v "$frames" \
        -lavfi "[0:v][1:v]psnr=stats_file=${stats}.psnr;[0:v][1:v]ssim=stats_file=${stats}.ssim" \
        -f null /dev/null 2>"${stats}.log"
    parse_quality_log "${stats}.log"
}

measure_quality_nv12() {
    local decoded_nv12="$1" ref_nv12="$2" frames="$3" stats="$4"
    local fps_num="$5" fps_den="$6"
    local fps=$((fps_num / fps_den))
    "$PREP_FFMPEG" -y -f rawvideo -pix_fmt nv12 -s "${WIDTH}x${HEIGHT}" -r "$fps" -i "$decoded_nv12" \
        -f rawvideo -pix_fmt nv12 -s "${WIDTH}x${HEIGHT}" -r "$fps" -i "$ref_nv12" \
        -frames:v "$frames" \
        -lavfi "[0:v][1:v]psnr=stats_file=${stats}.psnr;[0:v][1:v]ssim=stats_file=${stats}.ssim" \
        -f null /dev/null 2>"${stats}.log"
    parse_quality_log "${stats}.log"
}

actual_kbps() {
    local file="$1" frames="${2:-}" dur="${3:-$DURATION}"
    python3 - "$file" "$frames" "$dur" "$FPS_NUM" "$FPS_DEN" <<'PY'
import os, sys
size = os.path.getsize(sys.argv[1])
frames = sys.argv[2]
dur = float(sys.argv[3])
fps_num = float(sys.argv[4])
fps_den = float(sys.argv[5])
fps = fps_num / fps_den if fps_den else 30.0
if frames and frames != "0":
    dur = float(frames) / fps
if dur <= 0:
    print("0")
else:
    print(f"{size * 8 / dur / 1000:.2f}")
PY
}

# 写入当前 $CSV（本次路线的分片文件，非 rd_data.csv 追加）。
write_csv_row() {
    python3 - "$@" <<'PY'
import sys
csv, codec, tgt, br, q, t0, t1, t2 = sys.argv[1:9]
t3 = sys.argv[9] if len(sys.argv) > 9 else ""
enc = float(t1) - float(t0)
dec = float(t2) - float(t1)
post = float(t3) - float(t2) if t3 else 0.0
open(csv, "a").write(
    f"{codec},{tgt},{br},{q},{enc:.1f},{dec:.1f},0.000,0.000,{post:.3f}\n")
PY
}

write_csv_row_session() {
    python3 - "$@" <<'PY'
import sys
csv, codec, tgt, br, q, t0, t1, dec, rga, wr, post = sys.argv[1:12]
enc = float(t1) - float(t0)
open(csv, "a").write(
    f"{codec},{tgt},{br},{q},{enc:.1f},{float(dec):.3f},"
    f"{float(rga):.3f},{float(wr):.3f},{float(post):.3f}\n")
PY
}

align_even() {
    local v="$1"
    echo $(( v & ~1 ))
}

enc_dims() {
    ENC_W=$(align_even $(( WIDTH / ENC_SCALE_DENOM )))
    ENC_H=$(align_even $(( HEIGHT / ENC_SCALE_DENOM )))
}

# RKMPP CBR 在 2s 短片段上无法稳定命中目标码率（首 GOP 严重超发），
# 故 h264/h265 使用按分辨率校准的 CQP（rc_mode=2）。
# SVT preset 11 下 VBR/CBR 均难命中 target，calibrated 模式用 CRF/CQP 表扫点。
rkmpp_cqp_encode_args() {
    local qp="$1"
    printf '%s\n' -rc_mode 2 -qp_init "$qp"
}

svt_vbr_args() {
    local kbps="$1"
    printf '%s\n' --rc 1 --tbr "$kbps"
}

# 全分辨率 1080p @ locomotion 2s CRF 表（SVT preset 11，actual≈target）
svt_crf_for_target() {
    case "$1" in
         25) echo 70 ;;   30) echo 69 ;;   40) echo 68 ;;
         50) echo 70 ;;   60) echo 68 ;;   80) echo 67 ;;
        100) echo 65 ;;  150) echo 64 ;;  200) echo 62 ;;
        300) echo 60 ;;  400) echo 58 ;;  500) echo 57 ;;
        600) echo 54 ;;  700) echo 53 ;;  800) echo 52 ;;
        900) echo 42 ;; 1000) echo 41 ;;
        *) echo 62 ;;
    esac
}

# 低分辨率 360p @ locomotion 2s CQP 表（post-upscale SVT 路线）
svt_lo_qp_for_target() {
    case "$1" in
         25) echo 63 ;;   30) echo 62 ;;   40) echo 61 ;;
         50) echo 60 ;;   60) echo 59 ;;   80) echo 57 ;;
        100) echo 53 ;;  150) echo 51 ;;  200) echo 46 ;;
        300) echo 41 ;;  400) echo 38 ;;  500) echo 36 ;;
        600) echo 34 ;;  700) echo 31 ;;  800) echo 29 ;;
        900) echo 28 ;; 1000) echo 19 ;;
        *) echo 46 ;;
    esac
}

svt_full_encode_args() {
    local kbps="$1"
    if [[ "$SVT_RD_MODE" == "vbr" ]]; then
        svt_vbr_args "$kbps"
        return
    fi
    local crf
    crf=$(svt_crf_for_target "$kbps")
    printf '%s\n' --rc 0 --crf "$crf"
}

svt_lo_encode_args() {
    local kbps="$1"
    if [[ "$SVT_RD_MODE" == "vbr" ]]; then
        svt_vbr_args "$kbps"
        return
    fi
    local qp
    qp=$(svt_lo_qp_for_target "$kbps")
    printf '%s\n' --rc 0 --aq-mode 0 --qp "$qp"
}

svt_superres_codec_name() {
    echo "svt-av1+superres"
}

svt_superres_mode_label() {
    case "${SVT_SUPERRES_MODE:-4}" in
        1) echo "fixed/${SVT_SUPERRES_DENOM}" ;;
        3) echo "qthresh" ;;
        4) echo "auto" ;;
        *) echo "mode${SVT_SUPERRES_MODE}" ;;
    esac
}

svt_superres_cli_args() {
    printf '%s\n' \
        --superres-mode "$SVT_SUPERRES_MODE" \
        --superres-denom "$SVT_SUPERRES_DENOM" \
        --superres-kf-denom "$SVT_SUPERRES_KF_DENOM" \
        --superres-qthres "$SVT_SUPERRES_QTHRES" \
        --superres-kf-qthres "$SVT_SUPERRES_KF_QTHRES"
}

svt_superres_encode_args() {
    local kbps="$1"
    local -a base superres
    mapfile -t base < <(svt_full_encode_args "$kbps")
    mapfile -t superres < <(svt_superres_cli_args)
    printf '%s\n' "${base[@]}" "${superres[@]}"
}

# 全分辨率 1080p @ locomotion 4s middle CQP（h264_rkmpp qp≤51，复杂片段 floor ~650kbps）
h264_qp_for_target() {
    case "$1" in
         25|30|40|50|60|80|100|150|200) echo 51 ;;
        300) echo 47 ;;  400) echo 45 ;;  500) echo 43 ;;
        600) echo 41 ;;  700) echo 40 ;;  800) echo 38 ;;
        900) echo 38 ;; 1000) echo 37 ;;
        *) echo 51 ;;
    esac
}

h265_qp_for_target() {
    case "$1" in
         25|30|40|50|60|80) echo 51 ;;
        100) echo 48 ;;  150) echo 47 ;;  200) echo 46 ;;
        300) echo 43 ;;  400) echo 41 ;;  500) echo 40 ;;
        600) echo 38 ;;  700) echo 37 ;;  800) echo 36 ;;
        900) echo 34 ;; 1000) echo 33 ;;
        *) echo 41 ;;
    esac
}

# 低分辨率 360p @ locomotion 2s CQP 表（post-upscale RKMPP 路线）
h264_lo_qp_for_target() {
    case "$1" in
         25) echo 47 ;;   30) echo 46 ;;   40) echo 45 ;;
         50) echo 44 ;;   60) echo 43 ;;   80) echo 42 ;;
        100) echo 40 ;;  150) echo 39 ;;  200) echo 37 ;;
        300) echo 34 ;;  400) echo 32 ;;  500) echo 31 ;;
        600) echo 30 ;;  700) echo 29 ;;  800) echo 28 ;;
        900) echo 27 ;; 1000) echo 25 ;;
        *) echo 37 ;;
    esac
}

h265_lo_qp_for_target() {
    case "$1" in
         25) echo 42 ;;   30) echo 41 ;;   40) echo 40 ;;
         50) echo 39 ;;   60) echo 38 ;;   80) echo 37 ;;
        100) echo 35 ;;  150) echo 34 ;;  200) echo 33 ;;
        300) echo 32 ;;  400) echo 31 ;;  500) echo 29 ;;
        600) echo 27 ;;  700) echo 26 ;;  800) echo 25 ;;
        900) echo 24 ;; 1000) echo 23 ;;
        *) echo 33 ;;
    esac
}

sync_enc_ref_to_ram() {
    enc_dims
    REF_Y4M_ENC="$RAMDISK_DIR/clip_enc_${ENC_SCALE_DENOM}x_${ENC_W}x${ENC_H}.y4m"
    REF_NV12_ENC="$RAMDISK_DIR/clip_enc_${ENC_SCALE_DENOM}x_${ENC_W}x${ENC_H}.nv12"
    local enc_meta="$RAMDISK_DIR/clip_enc_${ENC_SCALE_DENOM}x.meta"
    local enc_method="rga-bilinear-nv12-v1"

    if [[ ! -f "$enc_meta" ]] || [[ "$(cat "$enc_meta")" != "$enc_method" ]] \
        || [[ ! -f "$REF_NV12_ENC" ]] || [[ "$REF_NV12_RAM" -nt "$REF_NV12_ENC" ]]; then
        echo "[prep] RGA 下采样参考帧 ${WIDTH}x${HEIGHT} → ${ENC_W}x${ENC_H} (1/${ENC_SCALE_DENOM}, bilinear, NV12) ..."
        downscale_nv12 "$REF_NV12_RAM" "$REF_NV12_ENC" "$FRAMES" || return 1
        echo "$enc_method" > "$enc_meta"
        rm -f "$REF_Y4M_ENC"
    fi
    if [[ ! -f "$REF_Y4M_ENC" ]] || [[ "$REF_NV12_ENC" -nt "$REF_Y4M_ENC" ]]; then
        "$FFMPEG" -y -f rawvideo -pix_fmt nv12 -video_size "${ENC_W}x${ENC_H}" \
            -framerate "$FPS_NUM/$FPS_DEN" -i "$REF_NV12_ENC" \
            -pix_fmt yuv420p -f yuv4mpegpipe "$REF_Y4M_ENC" 2>/dev/null
    fi
}

# IVF/AV1 等 drm_prime 输出需 hwdownload；MP4 走 RKMPP 硬解 + hwdownload。
ffmpeg_rkmpp_decoder_for_file() {
    local file="$1"
    local name
    name=$("$FFPROBE" -v error -select_streams v:0 -show_entries stream=codec_name \
        -of csv=p=0 "$file" 2>/dev/null || true)
    rkvc_rkmpp_decoder "$name"
}

ffmpeg_to_nv12_raw() {
    local input="$1" out_nv12="$2" log="$3"
    shift 3
    local dec

    if [[ "${input##*.}" == "ivf" ]]; then
        "$FFMPEG" -y -c:v av1_rkmpp -i "$input" \
            -vf "hwdownload,format=nv12" -pix_fmt nv12 \
            "$@" -f rawvideo "$out_nv12" 2>>"$log"
        return
    fi

    dec=$(ffmpeg_rkmpp_decoder_for_file "$input")
    "$FFMPEG" -y -c:v "$dec" -i "$input" -pix_fmt nv12 \
        "$@" -f rawvideo "$out_nv12" 2>>"$log"
}

ffmpeg_to_yuv420p_raw() {
    local input="$1" out_yuv="$2" log="$3"
    shift 3
    local dec

    if [[ "${input##*.}" == "ivf" ]]; then
        "$FFMPEG" -y -c:v av1_rkmpp -i "$input" \
            -vf "hwdownload,format=nv12" -pix_fmt yuv420p \
            "$@" -f rawvideo "$out_yuv" 2>>"$log"
        return
    fi

    dec=$(ffmpeg_rkmpp_decoder_for_file "$input")
    "$FFMPEG" -y -c:v "$dec" -i "$input" -pix_fmt yuv420p \
        "$@" -f rawvideo "$out_yuv" 2>>"$log"
}

# superres 码流：av1_rkmpp hwdownload 会因 stride/width 不一致崩溃，暂用软解。
ffmpeg_superres_to_yuv420p_raw() {
    local input="$1" out_yuv="$2" log="$3"
    shift 3
    local ff="${SVT_SUPERRES_FFMPEG:-/usr/bin/ffmpeg}"
    if [[ ! -x "$ff" ]]; then
        echo "[error] superres 解码需要 $ff（libaom-av1），或设置 SVT_SUPERRES_FFMPEG" >&2
        return 1
    fi
    "$ff" -y -c:v libaom-av1 -i "$input" -pix_fmt yuv420p \
        "$@" -f rawvideo "$out_yuv" 2>>"$log"
}


upscale_yuv() {
    local in_yuv="$1" out_yuv="$2" sw="$3" sh="$4" dw="$5" dh="$6" algo="$7" frames="$8"
    if [[ ! -x "$RKVC_YUV_UPSCALE" ]]; then
        echo "[error] RGA 缩放工具未构建: $RKVC_YUV_UPSCALE" >&2
        return 1
    fi
    env LD_LIBRARY_PATH="$LD_LIBRARY_PATH" "$RKVC_YUV_UPSCALE" --in "$in_yuv" --out "$out_yuv" \
        --sw "$sw" --sh "$sh" --dw "$dw" --dh "$dh" \
        --algo "$algo" --pix-fmt yuv420p --frames "$frames" || return 1
}

upscale_nv12() {
    local in_nv12="$1" out_nv12="$2" sw="$3" sh="$4" dw="$5" dh="$6" algo="$7" frames="$8"
    if [[ ! -x "$RKVC_YUV_UPSCALE" ]]; then
        echo "[error] RGA 缩放工具未构建: $RKVC_YUV_UPSCALE" >&2
        return 1
    fi
    env LD_LIBRARY_PATH="$LD_LIBRARY_PATH" "$RKVC_YUV_UPSCALE" --in "$in_nv12" --out "$out_nv12" \
        --sw "$sw" --sh "$sh" --dw "$dw" --dh "$dh" \
        --algo "$algo" --pix-fmt nv12 --frames "$frames" || return 1
}

# Session 硬解 (DMABUF) + RGA 上采样；输出 decode/rga/write/postproc 计时。
session_decode_upscale() {
    local bitstream="$1" out_nv12="$2" algo="$3" log="$4"
    if [[ ! -x "$RKVC_SESSION_UPSCALE" ]]; then
        echo "[error] Session 上采样工具未构建: $RKVC_SESSION_UPSCALE" >&2
        return 1
    fi
    "$RKVC_SESSION_UPSCALE" -i "$bitstream" -o "$out_nv12" \
        --width "$WIDTH" --height "$HEIGHT" \
        --enc-scale-denom "$ENC_SCALE_DENOM" \
        --post-upscale "$algo" --print-timing 2>"$log"
}

downscale_yuv() {
    local in_yuv="$1" out_yuv="$2" frames="$3"
    enc_dims
    upscale_yuv "$in_yuv" "$out_yuv" "$WIDTH" "$HEIGHT" "$ENC_W" "$ENC_H" bilinear "$frames"
}

downscale_nv12() {
    local in_nv12="$1" out_nv12="$2" frames="$3"
    enc_dims
    upscale_nv12 "$in_nv12" "$out_nv12" "$WIDTH" "$HEIGHT" "$ENC_W" "$ENC_H" bilinear "$frames"
}

post_upscale_codec_name() {
    local base="$1"
    local algo="$2"
    echo "${base}+up${ENC_SCALE_DENOM}x-${algo}"
}

post_upscale_base_enabled() {
    local base="$1"
    local algo name
    if codec_enabled "post-upscale-${base}"; then
        return 0
    fi
    IFS=',' read -ra _algos <<< "$UPSCALE_ALGOS"
    for algo in "${_algos[@]}"; do
        name=$(post_upscale_codec_name "$base" "$algo")
        codec_enabled "$name" && return 0
    done
    if codec_enabled post-upscale; then
        if [[ -n "${POST_UPSCALE_BASES:-}" ]]; then
            [[ ",$POST_UPSCALE_BASES," == *",$base,"* ]]
            return
        fi
        codec_enabled "$base" && return 0
    fi
    return 1
}

post_upscale_algo_enabled() {
    local base="$1"
    local algo="$2"
    local name
    name=$(post_upscale_codec_name "$base" "$algo")
    if codec_enabled "$name"; then
        return 0
    fi
    post_upscale_base_enabled "$base" && [[ ",$UPSCALE_ALGOS," == *",$algo,"* ]]
}

post_upscale_will_rerun() {
    local base algo
    for base in h264 h265 svt-av1; do
        if post_upscale_base_enabled "$base"; then
            return 0
        fi
    done
    return 1
}

run_h264_hw() {
    local kbps="$1"
    local ramdir logdir
    ramdir=$(bench_ramdir h264 "$kbps")
    logdir=$(bench_logdir h264 "$kbps")
    mkdir -p "$ramdir" "$logdir"
    local qp bitstream="$ramdir/stream.mp4" decoded_yuv="$ramdir/decoded.yuv" stats="$logdir/stats"
    local -a enc_args
    qp=$(h264_qp_for_target "$kbps")
    mapfile -t enc_args < <(rkmpp_cqp_encode_args "$qp")
    echo "[run] h264 RKMPP @ ${kbps}kbps (CQP qp=${qp}, raw ${WIDTH}x${HEIGHT})"
    local t0 t1 t2 br q
    t0=$(date +%s.%N)
    "$FFMPEG" -y -f rawvideo -pix_fmt yuv420p -video_size "${WIDTH}x${HEIGHT}" \
        -framerate "$FPS_NUM/$FPS_DEN" -i "$REF_YUV_RAM" -c:v h264_rkmpp \
        "${enc_args[@]}" -g 60 -an \
        "$bitstream" 2>"$logdir/enc.log"
    t1=$(date +%s.%N)
    ffmpeg_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$logdir/dec.log"
    t2=$(date +%s.%N)
    br=$(actual_kbps "$bitstream" "$FRAMES")
    q=$(measure_quality_yuv "$decoded_yuv" "$REF_YUV_RAM" "$FRAMES" "$stats" "$FPS_NUM" "$FPS_DEN")
    bench_cleanup_ramdir "$ramdir"
    write_csv_row "$CSV" "h264" "$kbps" "$br" "$q" "$t0" "$t1" "$t2"
}

run_h265_hw() {
    local kbps="$1"
    local ramdir logdir
    ramdir=$(bench_ramdir h265 "$kbps")
    logdir=$(bench_logdir h265 "$kbps")
    mkdir -p "$ramdir" "$logdir"
    local qp bitstream="$ramdir/stream.mp4" decoded_yuv="$ramdir/decoded.yuv" stats="$logdir/stats"
    local -a enc_args
    qp=$(h265_qp_for_target "$kbps")
    mapfile -t enc_args < <(rkmpp_cqp_encode_args "$qp")
    echo "[run] h265 RKMPP @ ${kbps}kbps (CQP qp=${qp}, raw ${WIDTH}x${HEIGHT})"
    local t0 t1 t2 br q
    t0=$(date +%s.%N)
    "$FFMPEG" -y -f rawvideo -pix_fmt yuv420p -video_size "${WIDTH}x${HEIGHT}" \
        -framerate "$FPS_NUM/$FPS_DEN" -i "$REF_YUV_RAM" -c:v hevc_rkmpp \
        "${enc_args[@]}" -g 60 -an \
        "$bitstream" 2>"$logdir/enc.log"
    t1=$(date +%s.%N)
    ffmpeg_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$logdir/dec.log"
    t2=$(date +%s.%N)
    br=$(actual_kbps "$bitstream" "$FRAMES")
    q=$(measure_quality_yuv "$decoded_yuv" "$REF_YUV_RAM" "$FRAMES" "$stats" "$FPS_NUM" "$FPS_DEN")
    bench_cleanup_ramdir "$ramdir"
    write_csv_row "$CSV" "h265" "$kbps" "$br" "$q" "$t0" "$t1" "$t2"
}

run_svt_av1() {
    local kbps="$1"
    local ramdir logdir
    ramdir=$(bench_ramdir svt-av1 "$kbps")
    logdir=$(bench_logdir svt-av1 "$kbps")
    mkdir -p "$ramdir" "$logdir"
    if [[ ! -x "$SVT_ENC" ]]; then
        echo "[skip] SVT-AV1 未构建: $SVT_ENC (运行 ./scripts/build-svt.sh)"
        return 0
    fi
    local bitstream="$ramdir/stream.ivf"
    local decoded_yuv="$ramdir/decoded.yuv" stats="$logdir/stats"
    local -a svt_args
    mapfile -t svt_args < <(svt_full_encode_args "$kbps")
    echo "[run] svt-av1 @ ${kbps}kbps (preset ${SVT_PRESET}, mode ${SVT_RD_MODE})"
    local t0 t1 t2 br q
    t0=$(date +%s.%N)
    "$SVT_ENC" --input "$REF_Y4M_RAM" -b "$bitstream" --preset "$SVT_PRESET" \
        "${svt_args[@]}" --keyint 60 --lp 4 -n "$FRAMES" 2>"$logdir/enc.log"
    t1=$(date +%s.%N)
    ffmpeg_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$logdir/dec.log"
    t2=$(date +%s.%N)
    br=$(actual_kbps "$bitstream" "$FRAMES")
    q=$(measure_quality_yuv "$decoded_yuv" "$REF_YUV_RAM" "$FRAMES" "$stats" "$FPS_NUM" "$FPS_DEN")
    bench_cleanup_ramdir "$ramdir"
    write_csv_row "$CSV" "svt-av1" "$kbps" "$br" "$q" "$t0" "$t1" "$t2"
}

run_svt_av1_superres() {
    local kbps="$1"
    local csv_codec
    csv_codec=$(svt_superres_codec_name)
    local ramdir logdir
    ramdir=$(bench_ramdir "$csv_codec" "$kbps")
    logdir=$(bench_logdir "$csv_codec" "$kbps")
    mkdir -p "$ramdir" "$logdir"
    if [[ ! -x "$SVT_ENC" ]]; then
        echo "[skip] SVT-AV1 未构建: $SVT_ENC (运行 ./scripts/build-svt.sh)"
        return 0
    fi
    local bitstream="$ramdir/stream.ivf"
    local decoded_yuv="$ramdir/decoded.yuv" stats="$logdir/stats"
    local -a svt_args
    mapfile -t svt_args < <(svt_superres_encode_args "$kbps")
    echo "[run] ${csv_codec} @ ${kbps}kbps (superres $(svt_superres_mode_label), preset ${SVT_PRESET}, mode ${SVT_RD_MODE})"
    echo "[warn] svt-av1+superres 为实验路线（搁置）；解码走 ${SVT_SUPERRES_FFMPEG:-/usr/bin/ffmpeg} libaom-av1 软解" >&2
    local t0 t1 t2 br q
    t0=$(date +%s.%N)
    "$SVT_ENC" --input "$REF_Y4M_RAM" -b "$bitstream" --preset "$SVT_PRESET" \
        "${svt_args[@]}" --keyint 60 --lp 4 -n "$FRAMES" 2>"$logdir/enc.log" || return 1
    t1=$(date +%s.%N)
    ffmpeg_superres_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$logdir/dec.log" -frames:v "$FRAMES" || return 1
    t2=$(date +%s.%N)
    br=$(actual_kbps "$bitstream" "$FRAMES")
    q=$(measure_quality_yuv "$decoded_yuv" "$REF_YUV_RAM" "$FRAMES" "$stats" "$FPS_NUM" "$FPS_DEN")
    bench_cleanup_ramdir "$ramdir"
    write_csv_row "$CSV" "$csv_codec" "$kbps" "$br" "$q" "$t0" "$t1" "$t2"
}

run_rkmpp_post_upscale() {
    local base="$1" algo="$2" kbps="$3"
    local csv_codec enc dec qp ramdir logdir
    csv_codec=$(post_upscale_codec_name "$base" "$algo")
    ramdir=$(bench_ramdir "$csv_codec" "$kbps")
    logdir=$(bench_logdir "$csv_codec" "$kbps")
    mkdir -p "$ramdir" "$logdir"
    case "$base" in
        h264)
            enc=h264_rkmpp
            dec=h264_rkmpp
            qp=$(h264_lo_qp_for_target "$kbps")
            ;;
        h265)
            enc=hevc_rkmpp
            dec=hevc_rkmpp
            qp=$(h265_lo_qp_for_target "$kbps")
            ;;
        *)
            echo "[error] 不支持的 post-upscale 基线: $base" >&2
            return 1
            ;;
    esac
    enc_dims
    sync_enc_ref_to_ram
    local bitstream="$ramdir/stream.mp4"
    local upscaled_nv12="$ramdir/upscaled.nv12"
    local stats="$logdir/stats"
    local -a enc_args
    mapfile -t enc_args < <(rkmpp_cqp_encode_args "$qp")
    echo "[run] ${csv_codec} @ ${kbps}kbps (encode ${ENC_W}x${ENC_H} CQP qp=${qp}, session decode+upscale=${algo})"
    local t0 t1 timing dec_sec post_sec br q
    t0=$(date +%s.%N)
    "$FFMPEG" -y -f rawvideo -pix_fmt nv12 -video_size "${ENC_W}x${ENC_H}" \
        -framerate "$FPS_NUM/$FPS_DEN" -i "$REF_NV12_ENC" -c:v "$enc" \
        "${enc_args[@]}" -g 60 -an "$bitstream" 2>"$logdir/enc.log" || return 1
    t1=$(date +%s.%N)
    timing=$(session_decode_upscale "$bitstream" "$upscaled_nv12" "$algo" "$logdir/dec.log") || return 1
    dec_sec=$(echo "$timing" | sed -n 's/.*decode_sec=\([0-9.]*\).*/\1/p')
    rga_sec=$(echo "$timing" | sed -n 's/.*rga_sec=\([0-9.]*\).*/\1/p')
    write_sec=$(echo "$timing" | sed -n 's/.*write_sec=\([0-9.]*\).*/\1/p')
    post_sec=$(echo "$timing" | sed -n 's/.*postproc_sec=\([0-9.]*\).*/\1/p')
    br=$(actual_kbps "$bitstream" "$FRAMES")
    q=$(measure_quality_nv12 "$upscaled_nv12" "$REF_NV12_RAM" "$FRAMES" "$stats" "$FPS_NUM" "$FPS_DEN")
    bench_cleanup_ramdir "$ramdir"
    write_csv_row_session "$CSV" "$csv_codec" "$kbps" "$br" "$q" "$t0" "$t1" \
        "$dec_sec" "$rga_sec" "$write_sec" "$post_sec"
}

run_svt_av1_post_upscale() {
    local algo="$1" kbps="$2"
    local csv_codec ramdir logdir
    csv_codec=$(post_upscale_codec_name svt-av1 "$algo")
    ramdir=$(bench_ramdir "$csv_codec" "$kbps")
    logdir=$(bench_logdir "$csv_codec" "$kbps")
    mkdir -p "$ramdir" "$logdir"
    if [[ ! -x "$SVT_ENC" ]]; then
        echo "[skip] SVT-AV1 未构建: $SVT_ENC (运行 ./scripts/build-svt.sh)"
        return 0
    fi
    enc_dims
    sync_enc_ref_to_ram
    local bitstream="$ramdir/stream.ivf"
    local upscaled_nv12="$ramdir/upscaled.nv12"
    local stats="$logdir/stats"
    echo "[run] ${csv_codec} @ ${kbps}kbps (encode ${ENC_W}x${ENC_H}, session decode+upscale=${algo}, preset ${SVT_PRESET}, mode ${SVT_RD_MODE})"
    local t0 t1 timing dec_sec rga_sec write_sec post_sec br q
    local -a svt_args
    mapfile -t svt_args < <(svt_lo_encode_args "$kbps")
    t0=$(date +%s.%N)
    "$SVT_ENC" --input "$REF_Y4M_ENC" -b "$bitstream" --preset "$SVT_PRESET" \
        "${svt_args[@]}" --keyint 60 --lp 4 -n "$FRAMES" 2>"$logdir/enc.log"
    t1=$(date +%s.%N)
    timing=$(session_decode_upscale "$bitstream" "$upscaled_nv12" "$algo" "$logdir/dec.log") || return 1
    dec_sec=$(echo "$timing" | sed -n 's/.*decode_sec=\([0-9.]*\).*/\1/p')
    rga_sec=$(echo "$timing" | sed -n 's/.*rga_sec=\([0-9.]*\).*/\1/p')
    write_sec=$(echo "$timing" | sed -n 's/.*write_sec=\([0-9.]*\).*/\1/p')
    post_sec=$(echo "$timing" | sed -n 's/.*postproc_sec=\([0-9.]*\).*/\1/p')
    br=$(actual_kbps "$bitstream" "$FRAMES")
    q=$(measure_quality_nv12 "$upscaled_nv12" "$REF_NV12_RAM" "$FRAMES" "$stats" "$FPS_NUM" "$FPS_DEN")
    bench_cleanup_ramdir "$ramdir"
    write_csv_row_session "$CSV" "$csv_codec" "$kbps" "$br" "$q" "$t0" "$t1" \
        "$dec_sec" "$rga_sec" "$write_sec" "$post_sec"
}

rkvc_rkmpp_decoder() {
    case "$1" in
        h264) echo h264_rkmpp ;;
        hevc) echo hevc_rkmpp ;;
        av1)  echo av1_rkmpp ;;
        *)    echo hevc_rkmpp ;;
    esac
}

stream_frame_count() {
    local file="$1"
    local count
    count=$("$FFPROBE" -v error -count_frames -select_streams v:0 \
        -show_entries stream=nb_read_frames -of csv=p=0 "$file" 2>/dev/null || echo 0)
    echo "${count:-0}"
}

run_rkvc_transcode_policy() {
    local policy="$1" kbps="$2"
    local csv_codec="rkvc-${policy}"
    local ramdir logdir
    ramdir=$(bench_ramdir "$csv_codec" "$kbps")
    logdir=$(bench_logdir "$csv_codec" "$kbps")
    mkdir -p "$ramdir" "$logdir"
    local bitstream="$ramdir/stream.mp4" decoded_yuv="$ramdir/decoded.yuv" stats="$logdir/stats"
    if [[ ! -x "$RKVC_TRANS" ]]; then
        echo "[skip] rkvc_transcode 未构建: $RKVC_TRANS"
        return 0
    fi
    local min_frames=$((FRAMES - 2))
    local attempt got t0 t1 t2 br q qp enc_frames
    for attempt in 1 2 3; do
        rm -f "$bitstream" "$decoded_yuv" "$logdir"/*.log "$logdir"/stats.*
        t0=$(date +%s.%N)
        case "$policy" in
            realtime)
                qp=$(h264_qp_for_target "$kbps")
                echo "[run] rkvc session (realtime → H.264, CQP qp=${qp}) @ ${kbps}kbps"
                "$RKVC_TRANS" -i "$SRC_CLIP" -o "$bitstream" -p realtime -b "$((kbps * 1000))" \
                    --rc-mode cqp --qp "$qp" -s "${WIDTH}x${HEIGHT}" 2>"$logdir/enc.log"
                ;;
            balanced)
                qp=$(h265_qp_for_target "$kbps")
                echo "[run] rkvc session (balanced → HEVC, CQP qp=${qp}) @ ${kbps}kbps"
                "$RKVC_TRANS" -i "$SRC_CLIP" -o "$bitstream" -p balanced -b "$((kbps * 1000))" \
                    --rc-mode cqp --qp "$qp" -s "${WIDTH}x${HEIGHT}" 2>"$logdir/enc.log"
                ;;
            *)
                echo "[error] 未知 rkvc policy: $policy" >&2
                return 1
                ;;
        esac
        got=$(stream_frame_count "$bitstream")
        if [[ "$got" -ge "$min_frames" ]]; then
            break
        fi
        echo "[warn] ${csv_codec} @ ${kbps}kbps 仅 ${got}/${FRAMES} 帧，重试..." >&2
    done
    enc_frames=$(stream_frame_count "$bitstream")
    if [[ "$enc_frames" -lt "$min_frames" ]]; then
        echo "[error] ${csv_codec} @ ${kbps}kbps 帧数不足: ${enc_frames}/${FRAMES}" >&2
        return 1
    fi
    t1=$(date +%s.%N)
    ffmpeg_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$logdir/dec.log"
    t2=$(date +%s.%N)
    br=$(actual_kbps "$bitstream" "$enc_frames")
    q=$(measure_quality_yuv "$decoded_yuv" "$REF_YUV_RAM" "$enc_frames" "$stats" "$FPS_NUM" "$FPS_DEN")
    bench_cleanup_ramdir "$ramdir"
    write_csv_row "$CSV" "$csv_codec" "$kbps" "$br" "$q" "$t0" "$t1" "$t2"
}

run_rkvc_quality() {
    local kbps="$1"
    local csv_codec="rkvc-quality"
    local ramdir logdir
    ramdir=$(bench_ramdir "$csv_codec" "$kbps")
    logdir=$(bench_logdir "$csv_codec" "$kbps")
    mkdir -p "$ramdir" "$logdir"
    local bitstream="$ramdir/stream.ivf" decoded_yuv="$ramdir/decoded.yuv" stats="$logdir/stats"
    local fps=$((FPS_NUM / FPS_DEN))
    if [[ ! -x "$RKVC_ENC" ]]; then
        echo "[skip] rkvc_encode 未构建: $RKVC_ENC"
        return 0
    fi
    echo "[run] rkvc session (quality → AV1 SVT p${SVT_PRESET}, YUV encode) @ ${kbps}kbps"
    local t0 t1 t2 br q enc_frames
    t0=$(date +%s.%N)
    "$RKVC_ENC" -i "$REF_YUV_RAM" -o "$bitstream" -p quality -b "$((kbps * 1000))" \
        --rc-mode vbr --pix-fmt yuv420p -s "${WIDTH}x${HEIGHT}" -r "$fps" 2>"$logdir/enc.log"
    t1=$(date +%s.%N)
    enc_frames=$(stream_frame_count "$bitstream")
    if [[ "$enc_frames" -lt $((FRAMES - 2)) ]]; then
        echo "[error] ${csv_codec} @ ${kbps}kbps 帧数不足: ${enc_frames}/${FRAMES}" >&2
        return 1
    fi
    ffmpeg_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$logdir/dec.log" -frames:v "$enc_frames"
    t2=$(date +%s.%N)
    br=$(actual_kbps "$bitstream" "$enc_frames")
    q=$(measure_quality_yuv "$decoded_yuv" "$REF_YUV_RAM" "$enc_frames" "$stats" "$FPS_NUM" "$FPS_DEN")
    bench_cleanup_ramdir "$ramdir"
    write_csv_row "$CSV" "$csv_codec" "$kbps" "$br" "$q" "$t0" "$t1" "$t2"
}

run_rkvc_realtime() { run_rkvc_transcode_policy realtime "$1"; }
run_rkvc_balanced()  { run_rkvc_transcode_policy balanced "$1"; }

bench_codec() {
    local fn="$1"
    local csv_part="$WORKDIR/results_${fn}.csv"
    echo "$CSV_HEADER" > "$csv_part"
    local orig_csv="$CSV"
    CSV="$csv_part"
    for kbps in "${BITRATES[@]}"; do
        "$fn" "$kbps"
    done
    CSV="$orig_csv"
}

codec_enabled() {
    [[ ",$RUN_CODECS," == *",$1,"* ]]
}

superres_enabled() {
    codec_enabled svt-av1+superres || codec_enabled svt-av1-superres
}

rkvc_policy_enabled() {
    [[ ",$RKVC_POLICIES," == *",$1,"* ]]
}

rkvc_policy_selected() {
    local policy="$1"
    local csv_codec="rkvc-${policy}"
    if codec_enabled "$csv_codec"; then
        return 0
    fi
    codec_enabled rkvc-v2 && rkvc_policy_enabled "$policy"
}

codec_will_rerun() {
    local codec="$1"
    if codec_enabled "$codec"; then
        return 0
    fi
    case "$codec" in
        rkvc-v2|rkvc-realtime|rkvc-balanced|rkvc-quality)
            codec_enabled rkvc-v2 && return 0
            ;;
    esac
    if [[ "$codec" == post-upscale ]]; then
        post_upscale_will_rerun && return 0
    fi
    if [[ "$codec" =~ ^(h264|h265|svt-av1)\+up[0-9]+x-(nearest|bilinear|bicubic)$ ]]; then
        post_upscale_algo_enabled "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" && return 0
    fi
    if [[ "$codec" =~ ^(h264|h265|svt-av1)\+up[0-9]+x$ ]]; then
        post_upscale_base_enabled "${BASH_REMATCH[1]}" && return 0
    fi
    return 1
}

echo "[info] project: $PROJECT_ROOT"
echo "[info] ffmpeg:  $FFMPEG"
echo "[info] clip: ${CLIP_SEC}s @ ${CLIP_START}s (${FRAMES} frames, ${WIDTH}x${HEIGHT}, offset=${CLIP_OFFSET})"
echo "[info] bitrates: ${BITRATES[*]} kbps (TARGET_KBPS)"
echo "[info] svt rd mode: $SVT_RD_MODE (calibrated=CRF/CQP, vbr=--rc 1 --tbr)"
if superres_enabled; then
    echo "[info] svt superres: mode=${SVT_SUPERRES_MODE} denom=${SVT_SUPERRES_DENOM} qthres=${SVT_SUPERRES_QTHRES}"
fi
echo "[info] codecs: $RUN_CODECS"
echo "[info] enc scale denom: $ENC_SCALE_DENOM  upscale algos: $UPSCALE_ALGOS"
echo "[info] rkvc policies: $RKVC_POLICIES"
echo "[info] rkvc: $RKVC_TRANS"
echo "[info] ramdisk: $RAMDISK_DIR (码流/YUV 中间文件 tmpfs)"

for codec in h264 h265 svt-av1 svt-av1+superres rkvc-v2 rkvc-realtime rkvc-balanced rkvc-quality; do
    codec_will_rerun "$codec" && rm -rf "$RAM_WORK_DIR/$codec"
done
if post_upscale_will_rerun; then
    IFS=',' read -ra _up_algos <<< "$UPSCALE_ALGOS"
    for base in h264 h265 svt-av1; do
        post_upscale_base_enabled "$base" || continue
        for algo in "${_up_algos[@]}"; do
            _c=$(post_upscale_codec_name "$base" "$algo")
            codec_will_rerun "$_c" && rm -rf "$RAM_WORK_DIR/$_c"
        done
    done
    sync_enc_ref_to_ram
fi
sync_ref_to_ram
rm -f "$WORKDIR"/results_*.csv

bench_rkvc_policies() {
    local policy fn
    for policy in realtime balanced quality; do
        if ! rkvc_policy_selected "$policy"; then
            continue
        fi
        fn="run_rkvc_${policy}"
        if [[ "$BENCH_PARALLEL" == "1" ]]; then
            bench_codec "$fn" &
            pids+=($!)
        else
            bench_codec "$fn"
        fi
    done
}

bench_post_upscale_combo() {
    local base="$1" algo="$2"
    local csv_part="$WORKDIR/results_post_${base}_${algo}.csv"
    echo "$CSV_HEADER" > "$csv_part"
    local orig_csv="$CSV"
    CSV="$csv_part"
    for kbps in "${BITRATES[@]}"; do
        case "$base" in
            h264|h265) run_rkmpp_post_upscale "$base" "$algo" "$kbps" ;;
            svt-av1)   run_svt_av1_post_upscale "$algo" "$kbps" ;;
        esac
    done
    CSV="$orig_csv"
}

bench_post_upscale() {
    local base algo
    IFS=',' read -ra _bench_algos <<< "$UPSCALE_ALGOS"
    for base in h264 h265 svt-av1; do
        post_upscale_base_enabled "$base" || continue
        for algo in "${_bench_algos[@]}"; do
            if ! post_upscale_algo_enabled "$base" "$algo"; then
                continue
            fi
            if [[ "$BENCH_PARALLEL" == "1" ]]; then
                bench_post_upscale_combo "$base" "$algo" &
                pids+=($!)
            else
                bench_post_upscale_combo "$base" "$algo"
            fi
        done
    done
}

BENCH_PARALLEL="${BENCH_PARALLEL:-0}"

if [[ "$BENCH_PARALLEL" == "1" ]]; then
    pids=()
    codec_enabled h264 && { bench_codec run_h264_hw & pids+=($!); }
    codec_enabled h265 && { bench_codec run_h265_hw & pids+=($!); }
    codec_enabled svt-av1 && { bench_codec run_svt_av1 & pids+=($!); }
    superres_enabled && { bench_codec run_svt_av1_superres & pids+=($!); }
    bench_rkvc_policies
    bench_post_upscale
    for pid in "${pids[@]}"; do wait "$pid"; done
else
    codec_enabled h264 && bench_codec run_h264_hw
    codec_enabled h265 && bench_codec run_h265_hw
    codec_enabled svt-av1 && bench_codec run_svt_av1
    superres_enabled && bench_codec run_svt_av1_superres
    bench_rkvc_policies
    bench_post_upscale
fi

finalize_csv

echo "[done] wrote $CSV"
plot_results "$FRAMES" 2>&1 | tee -a "$RESULTS/benchmark.log"
