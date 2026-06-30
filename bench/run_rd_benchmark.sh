#!/usr/bin/env bash
# bench/run_rd_benchmark.sh — RK3588 端到端 RD 基准（集成于 rkvc 项目）
#
# 对比路线（默认）: h264 / h265 / svt-av1 / rkvc-v2 / post-upscale
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

# shellcheck source=../scripts/build-common.sh
source "$PROJECT_ROOT/scripts/build-common.sh" 2>/dev/null || true

SRC_VIDEO="${1:-${SRC_VIDEO:-}}"
CLIP_SEC="${CLIP_SEC:-2}"
BITRATES=(200 400 600 800 1000)
SVT_PRESET="${SVT_PRESET:-11}"
ENC_SCALE_DENOM="${ENC_SCALE_DENOM:-2}"
UPSCALE_ALGOS="${UPSCALE_ALGOS:-nearest,bilinear,bicubic}"
RUN_CODECS="${RUN_CODECS:-h264,h265,svt-av1,rkvc-v2}"
RKVC_POLICIES="${RKVC_POLICIES:-realtime,balanced,quality}"
RAMDISK_DIR="${RAMDISK_DIR:-/dev/shm/rkvc-bench}"
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
RKVC_ENC="$RKVC_BUILD/rkvc_encode"

FFMPEG_LIB_DIRS=""
for _d in "$FFMPEG_SRC"/libav* "$FFMPEG_SRC"/libsw* "$FFMPEG_SRC"/libpostproc; do
    [[ -d "$_d" ]] && FFMPEG_LIB_DIRS="${_d}:${FFMPEG_LIB_DIRS}"
done
export LD_LIBRARY_PATH="$MPP_LIB:${FFMPEG_LIB_DIRS}$SVT_PREFIX/lib:${RKVC_BUILD}:${LD_LIBRARY_PATH:-}"
export PATH="$SVT_PREFIX/bin:${FFMPEG%/*}:$PATH"

mkdir -p "$RESULTS" "$WORKDIR" "$RAMDISK_DIR"

CLIP_MP4="$WORKDIR/clip.mp4"
REF_Y4M="$WORKDIR/clip.y4m"
REF_YUV="$WORKDIR/clip.yuv"
REF_Y4M_RAM="$RAMDISK_DIR/clip.y4m"
REF_YUV_RAM="$RAMDISK_DIR/clip.yuv"
CLIP_META="$WORKDIR/clip.meta"
CSV="$RESULTS/rd_data.csv"

usage() {
    cat <<EOF
用法: $(basename "$0") [源视频.mp4]

环境变量:
  SRC_VIDEO      源视频路径（也可作为第一个参数）
  RUN_CODECS     默认 h264,h265,svt-av1,rkvc-v2（rkvc-v2 展开为三档 policy）
  ENC_SCALE_DENOM  下采样编码分母（post-upscale 路线，默认 2）
  UPSCALE_ALGOS  后处理上采样算法列表（RGA 硬件，默认 nearest,bilinear,bicubic）
  RKVC_POLICIES  rkvc Session 语义档位（默认 realtime,balanced,quality）
  CLIP_SEC       截取秒数（默认 2）
  RKVC_BUILD     rkvc 构建目录（默认 $PROJECT_ROOT/build）
  PLOT_ONLY=1    仅根据已有 CSV 绘图
  RAMDISK_DIR    YUV 缓存目录（默认 /dev/shm/rkvc-bench）

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
        title_rd="E2E RD Curve (${CLIP_SEC}s, ${WIDTH}x${HEIGHT} vs ${ENC_W}x${ENC_H} + ${ENC_SCALE_DENOM}x upscale)"
        title_perf="E2E Performance (${CLIP_SEC}s, 1080p vs ${ENC_H}p+${ENC_SCALE_DENOM}x up)"
    else
        title_rd="E2E RD Curve (${CLIP_SEC}s, post-upscale ${ENC_SCALE_DENOM}x)"
        title_perf="E2E Performance (${CLIP_SEC}s, post-upscale ${ENC_SCALE_DENOM}x)"
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
    if [[ ! -f "$REF_YUV_RAM" ]] || [[ "$REF_YUV" -nt "$REF_YUV_RAM" ]]; then
        echo "[prep] 同步参考帧到 ${RAMDISK_DIR} ..."
        cp -f "$REF_Y4M" "$REF_Y4M_RAM"
        cp -f "$REF_YUV" "$REF_YUV_RAM"
    fi
}

prepare_clip() {
    if [[ ! -f "$CLIP_META" ]] || [[ "$(cat "$CLIP_META")" != "$CLIP_SEC" ]]; then
        echo "[prep] 截取 ${CLIP_SEC}s 片段 from $SRC_VIDEO ..."
        rm -f "$CLIP_MP4" "$REF_Y4M" "$REF_YUV"
        echo "$CLIP_SEC" > "$CLIP_META"
        "$PREP_FFMPEG" -y -ss 0 -t "$CLIP_SEC" -i "$SRC_VIDEO" -c copy -an "$CLIP_MP4" 2>/dev/null
        "$PREP_FFMPEG" -y -i "$CLIP_MP4" -pix_fmt yuv420p -f yuv4mpegpipe "$REF_Y4M" 2>/dev/null
        "$PREP_FFMPEG" -y -i "$CLIP_MP4" -pix_fmt yuv420p -f rawvideo "$REF_YUV" 2>/dev/null
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
    echo "codec,target_kbps,actual_kbps,psnr_y,psnr_u,psnr_v,psnr_avg,ssim,encode_sec,decode_sec,postproc_sec" > "$CSV"
elif ! head -1 "$CSV" | grep -q postproc_sec; then
    python3 - "$CSV" <<'PY'
import csv, sys
path = sys.argv[1]
fields = [
    "codec", "target_kbps", "actual_kbps", "psnr_y", "psnr_u", "psnr_v",
    "psnr_avg", "ssim", "encode_sec", "decode_sec", "postproc_sec",
]
rows = list(csv.DictReader(open(path, newline="")))
with open(path, "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=fields)
    w.writeheader()
    for r in rows:
        r.setdefault("postproc_sec", "0.0")
        w.writerow({k: r.get(k, "") for k in fields})
PY
fi

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

append_csv() {
    python3 - "$@" <<'PY'
import sys
csv, codec, tgt, br, q, t0, t1, t2 = sys.argv[1:9]
t3 = sys.argv[9] if len(sys.argv) > 9 else ""
enc = float(t1) - float(t0)
dec = float(t2) - float(t1)
post = float(t3) - float(t2) if t3 else 0.0
open(csv, "a").write(f"{codec},{tgt},{br},{q},{enc:.1f},{dec:.1f},{post:.1f}\n")
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
# 故 h264/h265 使用按分辨率校准的 CQP（rc_mode=2），SVT 仍用 --tbr。
rkmpp_cqp_encode_args() {
    local qp="$1"
    printf '%s\n' -rc_mode 2 -qp_init "$qp"
}

h264_qp_for_target() {
    case "$1" in
        200) echo 51 ;; 400) echo 49 ;; 600) echo 47 ;;
        800) echo 45 ;; 1000) echo 43 ;; *) echo 51 ;;
    esac
}

h265_qp_for_target() {
    case "$1" in
        200) echo 46 ;; 400) echo 41 ;; 600) echo 38 ;;
        800) echo 36 ;; 1000) echo 35 ;; *) echo 41 ;;
    esac
}

# 低分辨率编码（post-upscale）QP 表：按 640×360 @ 2s 片段校准，使 actual_kbps ≈ target。
h264_lo_qp_for_target() {
    case "$1" in
        200) echo 37 ;; 400) echo 32 ;; 600) echo 30 ;;
        800) echo 28 ;; 1000) echo 27 ;; *) echo 37 ;;
    esac
}

h265_lo_qp_for_target() {
    case "$1" in
        200) echo 33 ;; 400) echo 31 ;; 600) echo 27 ;;
        800) echo 25 ;; 1000) echo 24 ;; *) echo 33 ;;
    esac
}

sync_enc_ref_to_ram() {
    enc_dims
    REF_Y4M_ENC="$RAMDISK_DIR/clip_enc_${ENC_SCALE_DENOM}x_${ENC_W}x${ENC_H}.y4m"
    REF_YUV_ENC="$RAMDISK_DIR/clip_enc_${ENC_SCALE_DENOM}x_${ENC_W}x${ENC_H}.yuv"
    local enc_meta="$RAMDISK_DIR/clip_enc_${ENC_SCALE_DENOM}x.meta"
    local enc_method="rga-bilinear-v1"

    if [[ ! -f "$enc_meta" ]] || [[ "$(cat "$enc_meta")" != "$enc_method" ]] \
        || [[ ! -f "$REF_YUV_ENC" ]] || [[ "$REF_YUV_RAM" -nt "$REF_YUV_ENC" ]]; then
        echo "[prep] RGA 下采样参考帧 ${WIDTH}x${HEIGHT} → ${ENC_W}x${ENC_H} (1/${ENC_SCALE_DENOM}, bilinear) ..."
        downscale_yuv "$REF_YUV_RAM" "$REF_YUV_ENC" "$FRAMES" || return 1
        echo "$enc_method" > "$enc_meta"
        rm -f "$REF_Y4M_ENC"
    fi
    if [[ ! -f "$REF_Y4M_ENC" ]] || [[ "$REF_YUV_ENC" -nt "$REF_Y4M_ENC" ]]; then
        "$FFMPEG" -y -f rawvideo -pix_fmt yuv420p -video_size "${ENC_W}x${ENC_H}" \
            -framerate "$FPS_NUM/$FPS_DEN" -i "$REF_YUV_ENC" \
            -pix_fmt yuv420p -f yuv4mpegpipe "$REF_Y4M_ENC" 2>/dev/null
    fi
}

# IVF/AV1 等 drm_prime 输出需 hwdownload；MP4 走软解 h264/hevc。
ffmpeg_to_yuv420p_raw() {
    local input="$1" out_yuv="$2" log="$3"
    shift 3

    if [[ "${input##*.}" == "ivf" ]]; then
        "$FFMPEG" -y -c:v av1_rkmpp -i "$input" \
            -vf "hwdownload,format=nv12" -pix_fmt yuv420p \
            "$@" -f rawvideo "$out_yuv" 2>>"$log"
        return
    fi

    "$FFMPEG" -y -i "$input" -pix_fmt yuv420p \
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
        --algo "$algo" --frames "$frames" || return 1
}

downscale_yuv() {
    local in_yuv="$1" out_yuv="$2" frames="$3"
    enc_dims
    upscale_yuv "$in_yuv" "$out_yuv" "$WIDTH" "$HEIGHT" "$ENC_W" "$ENC_H" bilinear "$frames"
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
    local kbps="$1" outdir="$WORKDIR/h264/${kbps}"
    mkdir -p "$outdir"
    local qp bitstream="$outdir/stream.mp4" decoded_yuv="$outdir/decoded.yuv" stats="$outdir/stats"
    local -a enc_args
    qp=$(h264_qp_for_target "$kbps")
    mapfile -t enc_args < <(rkmpp_cqp_encode_args "$qp")
    echo "[run] h264 RKMPP @ ${kbps}kbps (CQP qp=${qp}, raw ${WIDTH}x${HEIGHT})"
    local t0 t1 t2 br q
    t0=$(date +%s.%N)
    "$FFMPEG" -y -f rawvideo -pix_fmt yuv420p -video_size "${WIDTH}x${HEIGHT}" \
        -framerate "$FPS_NUM/$FPS_DEN" -i "$REF_YUV_RAM" -c:v h264_rkmpp \
        "${enc_args[@]}" -g 60 -an \
        "$bitstream" 2>"$outdir/enc.log"
    t1=$(date +%s.%N)
    "$FFMPEG" -y -i "$bitstream" -c:v h264_rkmpp -f null /dev/null 2>"$outdir/dec.log"
    t2=$(date +%s.%N)
    ffmpeg_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$outdir/dec.log"
    br=$(actual_kbps "$bitstream" "$FRAMES")
    q=$(measure_quality_yuv "$decoded_yuv" "$REF_YUV_RAM" "$FRAMES" "$stats" "$FPS_NUM" "$FPS_DEN")
    append_csv "$CSV" "h264" "$kbps" "$br" "$q" "$t0" "$t1" "$t2"
}

run_h265_hw() {
    local kbps="$1" outdir="$WORKDIR/h265/${kbps}"
    mkdir -p "$outdir"
    local qp bitstream="$outdir/stream.mp4" decoded_yuv="$outdir/decoded.yuv" stats="$outdir/stats"
    local -a enc_args
    qp=$(h265_qp_for_target "$kbps")
    mapfile -t enc_args < <(rkmpp_cqp_encode_args "$qp")
    echo "[run] h265 RKMPP @ ${kbps}kbps (CQP qp=${qp}, raw ${WIDTH}x${HEIGHT})"
    local t0 t1 t2 br q
    t0=$(date +%s.%N)
    "$FFMPEG" -y -f rawvideo -pix_fmt yuv420p -video_size "${WIDTH}x${HEIGHT}" \
        -framerate "$FPS_NUM/$FPS_DEN" -i "$REF_YUV_RAM" -c:v hevc_rkmpp \
        "${enc_args[@]}" -g 60 -an \
        "$bitstream" 2>"$outdir/enc.log"
    t1=$(date +%s.%N)
    "$FFMPEG" -y -i "$bitstream" -c:v hevc_rkmpp -f null /dev/null 2>"$outdir/dec.log"
    t2=$(date +%s.%N)
    ffmpeg_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$outdir/dec.log"
    br=$(actual_kbps "$bitstream" "$FRAMES")
    q=$(measure_quality_yuv "$decoded_yuv" "$REF_YUV_RAM" "$FRAMES" "$stats" "$FPS_NUM" "$FPS_DEN")
    append_csv "$CSV" "h265" "$kbps" "$br" "$q" "$t0" "$t1" "$t2"
}

run_svt_av1() {
    local kbps="$1" outdir="$WORKDIR/svt-av1/${kbps}"
    mkdir -p "$outdir"
    if [[ ! -x "$SVT_ENC" ]]; then
        echo "[skip] SVT-AV1 未构建: $SVT_ENC (运行 ./scripts/build-svt.sh)"
        return 0
    fi
    local bitstream="$outdir/stream.ivf"
    local decoded_yuv stats="$outdir/stats"
    echo "[run] svt-av1 @ ${kbps}kbps (preset ${SVT_PRESET})"
    local t0 t1 t2 br q
    t0=$(date +%s.%N)
    "$SVT_ENC" --input "$REF_Y4M_RAM" -b "$bitstream" --preset "$SVT_PRESET" --rc 1 --tbr "$kbps" \
        --keyint 60 --lp 4 -n "$FRAMES" 2>"$outdir/enc.log"
    t1=$(date +%s.%N)
    mkdir -p "$RAMDISK_DIR/svt-av1/${kbps}"
    decoded_yuv="$RAMDISK_DIR/svt-av1/${kbps}/decoded.yuv"
    "$FFMPEG" -y -c:v av1_rkmpp -i "$bitstream" -f null /dev/null 2>"$outdir/dec.log"
    t2=$(date +%s.%N)
    ffmpeg_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$outdir/dec.log"
    br=$(actual_kbps "$bitstream" "$FRAMES")
    q=$(measure_quality_yuv "$decoded_yuv" "$REF_YUV_RAM" "$FRAMES" "$stats" "$FPS_NUM" "$FPS_DEN")
    append_csv "$CSV" "svt-av1" "$kbps" "$br" "$q" "$t0" "$t1" "$t2"
}

run_rkmpp_post_upscale() {
    local base="$1" algo="$2" kbps="$3"
    local csv_codec enc dec qp
    csv_codec=$(post_upscale_codec_name "$base" "$algo")
    local outdir="$WORKDIR/${csv_codec}/${kbps}"
    mkdir -p "$outdir"
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
    local bitstream="$outdir/stream.mp4"
    local decoded_yuv="$outdir/decoded_lo.yuv"
    local upscaled_yuv="$outdir/upscaled.yuv"
    local stats="$outdir/stats"
    local -a enc_args
    mapfile -t enc_args < <(rkmpp_cqp_encode_args "$qp")
    echo "[run] ${csv_codec} @ ${kbps}kbps (encode ${ENC_W}x${ENC_H} CQP qp=${qp}, upscale=${algo})"
    local t0 t1 t2 t3 br q
    t0=$(date +%s.%N)
    "$FFMPEG" -y -f rawvideo -pix_fmt yuv420p -video_size "${ENC_W}x${ENC_H}" \
        -framerate "$FPS_NUM/$FPS_DEN" -i "$REF_YUV_ENC" -c:v "$enc" \
        "${enc_args[@]}" -g 60 -an "$bitstream" 2>"$outdir/enc.log" || return 1
    t1=$(date +%s.%N)
    "$FFMPEG" -y -i "$bitstream" -c:v "$dec" -f null /dev/null 2>"$outdir/dec.log"
    t2=$(date +%s.%N)
    ffmpeg_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$outdir/dec.log" -frames:v "$FRAMES"
    upscale_yuv "$decoded_yuv" "$upscaled_yuv" "$ENC_W" "$ENC_H" "$WIDTH" "$HEIGHT" "$algo" "$FRAMES" || return 1
    t3=$(date +%s.%N)
    br=$(actual_kbps "$bitstream" "$FRAMES")
    q=$(measure_quality_yuv "$upscaled_yuv" "$REF_YUV_RAM" "$FRAMES" "$stats" "$FPS_NUM" "$FPS_DEN")
    append_csv "$CSV" "$csv_codec" "$kbps" "$br" "$q" "$t0" "$t1" "$t2" "$t3"
}

run_svt_av1_post_upscale() {
    local algo="$1" kbps="$2"
    local csv_codec
    csv_codec=$(post_upscale_codec_name svt-av1 "$algo")
    local outdir="$WORKDIR/${csv_codec}/${kbps}"
    mkdir -p "$outdir"
    if [[ ! -x "$SVT_ENC" ]]; then
        echo "[skip] SVT-AV1 未构建: $SVT_ENC (运行 ./scripts/build-svt.sh)"
        return 0
    fi
    enc_dims
    sync_enc_ref_to_ram
    local bitstream="$outdir/stream.ivf"
    local decoded_yuv="$outdir/decoded_lo.yuv"
    local upscaled_yuv="$outdir/upscaled.yuv"
    local stats="$outdir/stats"
    echo "[run] ${csv_codec} @ ${kbps}kbps (encode ${ENC_W}x${ENC_H}, upscale=${algo}, preset ${SVT_PRESET})"
    local t0 t1 t2 t3 br q
    t0=$(date +%s.%N)
    "$SVT_ENC" --input "$REF_Y4M_ENC" -b "$bitstream" --preset "$SVT_PRESET" --rc 1 --tbr "$kbps" \
        --keyint 60 --lp 4 -n "$FRAMES" 2>"$outdir/enc.log"
    t1=$(date +%s.%N)
    "$FFMPEG" -y -c:v av1_rkmpp -i "$bitstream" -f null /dev/null 2>"$outdir/dec.log"
    t2=$(date +%s.%N)
    ffmpeg_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$outdir/dec.log" -frames:v "$FRAMES"
    upscale_yuv "$decoded_yuv" "$upscaled_yuv" "$ENC_W" "$ENC_H" "$WIDTH" "$HEIGHT" "$algo" "$FRAMES" || return 1
    t3=$(date +%s.%N)
    br=$(actual_kbps "$bitstream" "$FRAMES")
    q=$(measure_quality_yuv "$upscaled_yuv" "$REF_YUV_RAM" "$FRAMES" "$stats" "$FPS_NUM" "$FPS_DEN")
    append_csv "$CSV" "$csv_codec" "$kbps" "$br" "$q" "$t0" "$t1" "$t2" "$t3"
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
    local outdir="$WORKDIR/${csv_codec}/${kbps}"
    mkdir -p "$outdir"
    local bitstream="$outdir/stream.mp4" decoded_yuv="$outdir/decoded.yuv" stats="$outdir/stats"
    if [[ ! -x "$RKVC_TRANS" ]]; then
        echo "[skip] rkvc_transcode 未构建: $RKVC_TRANS"
        return 0
    fi
    local min_frames=$((FRAMES - 2))
    local attempt got t0 t1 t2 br q dec_codec dec_v qp enc_frames
    for attempt in 1 2 3; do
        rm -f "$bitstream" "$decoded_yuv" "$outdir"/*.log "$outdir"/stats.*
        t0=$(date +%s.%N)
        case "$policy" in
            realtime)
                qp=$(h264_qp_for_target "$kbps")
                echo "[run] rkvc session (realtime → H.264, CQP qp=${qp}) @ ${kbps}kbps"
                "$RKVC_TRANS" -i "$SRC_CLIP" -o "$bitstream" -p realtime -b "$((kbps * 1000))" \
                    --rc-mode cqp --qp "$qp" -s "${WIDTH}x${HEIGHT}" 2>"$outdir/enc.log"
                ;;
            balanced)
                qp=$(h265_qp_for_target "$kbps")
                echo "[run] rkvc session (balanced → HEVC, CQP qp=${qp}) @ ${kbps}kbps"
                "$RKVC_TRANS" -i "$SRC_CLIP" -o "$bitstream" -p balanced -b "$((kbps * 1000))" \
                    --rc-mode cqp --qp "$qp" -s "${WIDTH}x${HEIGHT}" 2>"$outdir/enc.log"
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
    dec_codec=$("$FFPROBE" -v error -select_streams v:0 -show_entries stream=codec_name \
        -of csv=p=0 "$bitstream" 2>/dev/null || echo "hevc")
    dec_v=$(rkvc_rkmpp_decoder "$dec_codec")
    "$FFMPEG" -y -c:v "$dec_v" -i "$bitstream" -f null /dev/null 2>"$outdir/dec.log"
    t2=$(date +%s.%N)
    ffmpeg_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$outdir/dec.log"
    br=$(actual_kbps "$bitstream" "$enc_frames")
    q=$(measure_quality_yuv "$decoded_yuv" "$REF_YUV_RAM" "$enc_frames" "$stats" "$FPS_NUM" "$FPS_DEN")
    append_csv "$CSV" "$csv_codec" "$kbps" "$br" "$q" "$t0" "$t1" "$t2"
}

run_rkvc_quality() {
    local kbps="$1"
    local csv_codec="rkvc-quality"
    local outdir="$WORKDIR/${csv_codec}/${kbps}"
    mkdir -p "$outdir"
    local bitstream="$outdir/stream.ivf" decoded_yuv="$outdir/decoded.yuv" stats="$outdir/stats"
    local fps=$((FPS_NUM / FPS_DEN))
    if [[ ! -x "$RKVC_ENC" ]]; then
        echo "[skip] rkvc_encode 未构建: $RKVC_ENC"
        return 0
    fi
    echo "[run] rkvc session (quality → AV1 SVT p${SVT_PRESET}, YUV encode) @ ${kbps}kbps"
    local t0 t1 t2 br q dec_v enc_frames
    t0=$(date +%s.%N)
    "$RKVC_ENC" -i "$REF_YUV_RAM" -o "$bitstream" -p quality -b "$((kbps * 1000))" \
        --rc-mode vbr --pix-fmt yuv420p -s "${WIDTH}x${HEIGHT}" -r "$fps" 2>"$outdir/enc.log"
    t1=$(date +%s.%N)
    enc_frames=$(stream_frame_count "$bitstream")
    if [[ "$enc_frames" -lt $((FRAMES - 2)) ]]; then
        echo "[error] ${csv_codec} @ ${kbps}kbps 帧数不足: ${enc_frames}/${FRAMES}" >&2
        return 1
    fi
    dec_v=$(rkvc_rkmpp_decoder av1)
    "$FFMPEG" -y -c:v "$dec_v" -i "$bitstream" -f null /dev/null 2>"$outdir/dec.log"
    t2=$(date +%s.%N)
    ffmpeg_to_yuv420p_raw "$bitstream" "$decoded_yuv" "$outdir/dec.log" -frames:v "$enc_frames"
    br=$(actual_kbps "$bitstream" "$enc_frames")
    q=$(measure_quality_yuv "$decoded_yuv" "$REF_YUV_RAM" "$enc_frames" "$stats" "$FPS_NUM" "$FPS_DEN")
    append_csv "$CSV" "$csv_codec" "$kbps" "$br" "$q" "$t0" "$t1" "$t2"
}

run_rkvc_realtime() { run_rkvc_transcode_policy realtime "$1"; }
run_rkvc_balanced()  { run_rkvc_transcode_policy balanced "$1"; }

bench_codec() {
    local fn="$1"
    local csv_part="$WORKDIR/results_${fn}.csv"
    echo "codec,target_kbps,actual_kbps,psnr_y,psnr_u,psnr_v,psnr_avg,ssim,encode_sec,decode_sec,postproc_sec" > "$csv_part"
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
    if [[ "$codec" == post-upscale ]] || [[ "$codec" =~ ^(h264|h265|svt-av1)\+up[0-9]+x- ]]; then
        post_upscale_will_rerun && return 0
    fi
    return 1
}

strip_csv_codecs() {
    local keep=()
    while IFS= read -r line; do
        local codec="${line%%,*}"
        codec_will_rerun "$codec" && continue
        keep+=("$line")
    done < <(tail -n +2 "$CSV" 2>/dev/null || true)
    {
        head -1 "$CSV"
        if ((${#keep[@]})); then printf '%s\n' "${keep[@]}"; fi
    } > "${CSV}.tmp"
    mv "${CSV}.tmp" "$CSV"
}

echo "[info] project: $PROJECT_ROOT"
echo "[info] ffmpeg:  $FFMPEG"
echo "[info] clip: ${CLIP_SEC}s (${FRAMES} frames, ${WIDTH}x${HEIGHT})"
echo "[info] bitrates: ${BITRATES[*]} kbps"
echo "[info] codecs: $RUN_CODECS"
echo "[info] enc scale denom: $ENC_SCALE_DENOM  upscale algos: $UPSCALE_ALGOS"
echo "[info] rkvc policies: $RKVC_POLICIES"
echo "[info] rkvc: $RKVC_TRANS"

strip_csv_codecs

for codec in h264 h265 svt-av1 rkvc-v2 rkvc-realtime rkvc-balanced rkvc-quality; do
    codec_will_rerun "$codec" && rm -rf "$WORKDIR/$codec"
done
if post_upscale_will_rerun; then
    IFS=',' read -ra _up_algos <<< "$UPSCALE_ALGOS"
    for base in h264 h265 svt-av1; do
        post_upscale_base_enabled "$base" || continue
        for algo in "${_up_algos[@]}"; do
            _c=$(post_upscale_codec_name "$base" "$algo")
            codec_will_rerun "$_c" && rm -rf "$WORKDIR/$_c"
        done
    done
    sync_enc_ref_to_ram
fi
rm -rf "$RAMDISK_DIR"/svt-av1
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
    echo "codec,target_kbps,actual_kbps,psnr_y,psnr_u,psnr_v,psnr_avg,ssim,encode_sec,decode_sec,postproc_sec" > "$csv_part"
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
    bench_rkvc_policies
    bench_post_upscale
    for pid in "${pids[@]}"; do wait "$pid"; done
else
    codec_enabled h264 && bench_codec run_h264_hw
    codec_enabled h265 && bench_codec run_h265_hw
    codec_enabled svt-av1 && bench_codec run_svt_av1
    bench_rkvc_policies
    bench_post_upscale
fi

{
    head -1 "$CSV"
    tail -n +2 "$CSV"
    for f in "$WORKDIR"/results_*.csv; do
        [[ -f "$f" ]] && tail -n +2 "$f"
    done
} > "${CSV}.merged"
mv "${CSV}.merged" "$CSV"

echo "[done] wrote $CSV"
plot_results "$FRAMES" 2>&1 | tee -a "$RESULTS/benchmark.log"
