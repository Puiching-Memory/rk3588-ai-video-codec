#!/usr/bin/env python3
"""绘制端到端编解码 RD 曲线（码率 ≤ 1000 kbps）。"""

from __future__ import annotations

import argparse
import csv
import re
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt

UPSCALE_CODEC_RE = re.compile(
    r"^(?P<base>h264|h265|svt-av1)\+up(?P<scale>\d+)x-(?P<algo>nearest|bilinear|bicubic)$"
)

# 三种传统插值合并展示时的组键，例如 h264+up3x
UPSCALE_GROUP_RE = re.compile(
    r"^(?P<base>h264|h265|svt-av1)\+up(?P<scale>\d+)x$"
)

CODEC_LABELS = {
    "h264": "H.264 (RKMPP)",
    "h265": "H.265 (RKMPP)",
    "svt-av1": "SVT-AV1 (1080p)",
    "rkvc-realtime": "rkvc realtime (H.264)",
    "rkvc-balanced": "rkvc balanced (HEVC)",
    "rkvc-quality": "rkvc quality (AV1)",
    "rkvc-v2": "rkvc v2 (Session)",
}

CODEC_COLORS = {
    "h264": "#1f77b4",
    "h265": "#ff7f0e",
    "svt-av1": "#2ca02c",
    "rkvc-realtime": "#6baed6",
    "rkvc-balanced": "#fdae6b",
    "rkvc-quality": "#74c476",
    "rkvc-v2": "#9467bd",
}

UPSCALE_BASE_LABELS = {
    "h264": "H.264",
    "h265": "H.265",
    "svt-av1": "SVT-AV1",
}

UPSCALE_ALGO_COLORS = {
    "h264": {
        "nearest": "#aec7e8",
        "bilinear": "#6baed6",
        "bicubic": "#3182bd",
    },
    "h265": {
        "nearest": "#fdd0a2",
        "bilinear": "#fdae6b",
        "bicubic": "#f16913",
    },
    "svt-av1": {
        "nearest": "#98df8a",
        "bilinear": "#31a354",
        "bicubic": "#006d2c",
    },
}

UPSCALE_ALGO_MARKERS = {
    "nearest": "x",
    "bilinear": "d",
    "bicubic": "p",
}

# 合并后的 post-upscale 曲线颜色（与对应基线同色系、略浅）
UPSCALE_GROUP_COLORS = {
    "h264": "#6baed6",
    "h265": "#fdae6b",
    "svt-av1": "#31a354",
}

CODEC_MARKERS = {
    "h264": "o",
    "h265": "s",
    "svt-av1": "^",
    "rkvc-realtime": "D",
    "rkvc-balanced": "v",
    "rkvc-quality": "P",
    "rkvc-v2": "v",
}

CODEC_ORDER = [
    "h264",
    "h265",
    "svt-av1",
    "rkvc-realtime",
    "rkvc-balanced",
    "rkvc-quality",
    "rkvc-v2",
]


def is_post_upscale(codec: str) -> bool:
    return bool(UPSCALE_CODEC_RE.match(codec))


def is_upscale_group(codec: str) -> bool:
    return bool(UPSCALE_GROUP_RE.match(codec))


def upscale_group_key(codec: str) -> str | None:
    m = UPSCALE_CODEC_RE.match(codec)
    if not m:
        return None
    return f"{m.group('base')}+up{m.group('scale')}x"


def upscale_group_base(codec: str) -> str | None:
    m = UPSCALE_GROUP_RE.match(codec) or UPSCALE_CODEC_RE.match(codec)
    return m.group("base") if m else None


def upscale_group_scale(codec: str) -> int | None:
    m = UPSCALE_GROUP_RE.match(codec) or UPSCALE_CODEC_RE.match(codec)
    return int(m.group("scale")) if m else None


def upscale_group_label(codec: str) -> str:
    base = upscale_group_base(codec) or codec
    scale = upscale_group_scale(codec) or 0
    lo_h = 1080 // scale if scale else 0
    name = UPSCALE_BASE_LABELS.get(base, base)
    return f"{name} {lo_h}p→1080p RGA (nearest/bilinear/bicubic)"


def upscale_group_color(codec: str) -> str:
    base = upscale_group_base(codec) or codec
    return UPSCALE_GROUP_COLORS.get(base, "#888888")


def codec_label(codec: str) -> str:
    if is_upscale_group(codec):
        return upscale_group_label(codec)
    m = UPSCALE_CODEC_RE.match(codec)
    if m:
        base = m.group("base")
        scale = int(m.group("scale"))
        algo = m.group("algo")
        lo_h = 1080 // scale if scale else 0
        return f"{UPSCALE_BASE_LABELS.get(base, base)} {lo_h}p→1080p RGA ({algo})"
    return CODEC_LABELS.get(codec, codec)


def codec_color(codec: str) -> str | None:
    if is_upscale_group(codec):
        return upscale_group_color(codec)
    m = UPSCALE_CODEC_RE.match(codec)
    if m:
        return UPSCALE_ALGO_COLORS.get(m.group("base"), {}).get(m.group("algo"))
    return CODEC_COLORS.get(codec)


def codec_marker(codec: str) -> str:
    if is_upscale_group(codec):
        return "D"
    m = UPSCALE_CODEC_RE.match(codec)
    if m:
        return UPSCALE_ALGO_MARKERS.get(m.group("algo"), "o")
    return CODEC_MARKERS.get(codec, "o")


def sort_codecs(codecs: list[str]) -> list[str]:
    groups_present = sorted({gk for c in codecs if (gk := upscale_group_key(c))})
    baselines = [c for c in codecs if upscale_group_key(c) is None]

    ordered: list[str] = []
    for c in CODEC_ORDER:
        if c in baselines:
            ordered.append(c)
        for g in groups_present:
            if g.startswith(f"{c}+up"):
                ordered.append(g)
    for c in baselines:
        if c not in ordered:
            ordered.append(c)
    return ordered


def group_upscale_rd(data: dict[str, list[dict]]) -> dict[str, list[dict]]:
    """将三种插值曲线按 target_kbps 聚合为均值 ± min/max 带。"""
    grouped: dict[str, dict[float, list[dict]]] = defaultdict(lambda: defaultdict(list))
    baselines: dict[str, list[dict]] = {}

    for codec, pts in data.items():
        gk = upscale_group_key(codec)
        if gk:
            for p in pts:
                grouped[gk][p["target_kbps"]].append(p)
        else:
            baselines[codec] = pts

    out = dict(baselines)
    for gk, by_target in grouped.items():
        merged: list[dict] = []
        for target in sorted(by_target):
            rows = by_target[target]
            brs = [r["actual_kbps"] for r in rows]
            psnr = [r["psnr_y"] for r in rows]
            ssim = [r["ssim"] for r in rows]
            merged.append(
                {
                    "target_kbps": target,
                    "actual_kbps": statistics.mean(brs),
                    "actual_kbps_lo": min(brs),
                    "actual_kbps_hi": max(brs),
                    "psnr_y": statistics.mean(psnr),
                    "psnr_y_lo": min(psnr),
                    "psnr_y_hi": max(psnr),
                    "ssim": statistics.mean(ssim),
                    "ssim_lo": min(ssim),
                    "ssim_hi": max(ssim),
                }
            )
        out[gk] = merged
    return out


def load_csv(path: Path) -> dict[str, list[dict]]:
    data: dict[str, list[dict]] = defaultdict(list)
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            codec = row["codec"]
            data[codec].append(
                {
                    "target_kbps": float(row["target_kbps"]),
                    "actual_kbps": float(row["actual_kbps"]),
                    "psnr_y": float(row["psnr_y"]),
                    "psnr_avg": float(row["psnr_avg"]),
                    "ssim": float(row["ssim"]),
                }
            )
    for codec in data:
        data[codec] = [p for p in data[codec] if p["actual_kbps"] <= 1050]
        data[codec].sort(key=lambda x: x["actual_kbps"])
    return data


def plot_rd(data: dict[str, list[dict]], out_prefix: Path, title: str) -> None:
    data = group_upscale_rd(data)

    plt.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "axes.unicode_minus": False,
            "figure.dpi": 150,
        }
    )

    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    ax_psnr, ax_ssim = axes

    all_br: list[float] = []
    all_psnr: list[float] = []
    all_ssim: list[float] = []

    for codec in sort_codecs(list(data.keys())):
        if codec not in data:
            continue
        pts = data[codec]
        br = [p["actual_kbps"] for p in pts]
        psnr = [p["psnr_y"] for p in pts]
        ssim = [p["ssim"] for p in pts]
        all_br.extend(br)
        all_psnr.extend(psnr)
        all_ssim.extend(ssim)
        if is_upscale_group(codec):
            all_psnr.extend(p["psnr_y_lo"] for p in pts)
            all_psnr.extend(p["psnr_y_hi"] for p in pts)
            all_ssim.extend(p["ssim_lo"] for p in pts)
            all_ssim.extend(p["ssim_hi"] for p in pts)

        color = upscale_group_color(codec) if is_upscale_group(codec) else codec_color(codec)
        label = codec_label(codec)
        if is_upscale_group(codec):
            psnr_lo = [p["psnr_y_lo"] for p in pts]
            psnr_hi = [p["psnr_y_hi"] for p in pts]
            ssim_lo = [p["ssim_lo"] for p in pts]
            ssim_hi = [p["ssim_hi"] for p in pts]
            ax_psnr.fill_between(br, psnr_lo, psnr_hi, color=color, alpha=0.22, linewidth=0)
            ax_ssim.fill_between(br, ssim_lo, ssim_hi, color=color, alpha=0.22, linewidth=0)
            kw = dict(
                label=label,
                color=color,
                marker=codec_marker(codec),
                linewidth=2,
                linestyle="--",
                markersize=7,
                clip_on=True,
            )
            ax_psnr.plot(br, psnr, **kw)
            ax_ssim.plot(br, ssim, **kw)
        else:
            kw = dict(
                label=label,
                color=color,
                marker=codec_marker(codec),
                linewidth=2,
                markersize=7,
                clip_on=True,
            )
            ax_psnr.plot(br, psnr, **kw)
            ax_ssim.plot(br, ssim, **kw)

    if not all_br:
        raise SystemExit("无有效码率点可绘图")

    br_min, br_max = min(all_br), max(all_br)
    psnr_min, psnr_max = min(all_psnr), max(all_psnr)
    ssim_min, ssim_max = min(all_ssim), max(all_ssim)
    br_pad = max(50, (br_max - br_min) * 0.08)
    psnr_pad = max(0.5, (psnr_max - psnr_min) * 0.12)
    ssim_pad = max(0.01, (ssim_max - ssim_min) * 0.12)

    ax_psnr.set_xlim(br_min - br_pad, br_max + br_pad)
    ax_psnr.set_ylim(psnr_min - psnr_pad, psnr_max + psnr_pad)
    ax_ssim.set_xlim(br_min - br_pad, br_max + br_pad)
    ax_ssim.set_ylim(ssim_min - ssim_pad, min(1.0, ssim_max + ssim_pad))

    for ax, ylabel in ((ax_psnr, "PSNR-Y (dB)"), (ax_ssim, "SSIM")):
        ax.set_xlabel("Actual Bitrate (kbps)")
        ax.set_ylabel(ylabel)
        ax.grid(True, linestyle="--", alpha=0.45)
        ax.legend(loc="lower right", fontsize=7, framealpha=0.9)

    fig.suptitle(title, fontsize=12, fontweight="bold", y=1.02)
    fig.tight_layout()
    png = out_prefix.with_suffix(".png")
    pdf = out_prefix.with_suffix(".pdf")
    fig.savefig(png, bbox_inches="tight", pad_inches=0.25)
    fig.savefig(pdf, bbox_inches="tight", pad_inches=0.25)
    print(f"Saved: {png}")
    print(f"Saved: {pdf}")
    plt.close(fig)


def main() -> None:
    bench_root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="绘制 RD 曲线")
    parser.add_argument("--csv", type=Path, default=bench_root / "results" / "rd_data.csv")
    parser.add_argument("--out", type=Path, default=bench_root / "results" / "rd_curve_e2e")
    parser.add_argument(
        "--title",
        default="E2E RD Curve (RK3588, baselines + rkvc realtime/balanced/quality)",
    )
    args = parser.parse_args()

    if not args.csv.exists():
        raise SystemExit(f"找不到数据文件: {args.csv}")

    data = load_csv(args.csv)
    if not data:
        raise SystemExit("CSV 无有效数据")
    plot_rd(data, args.out, args.title)


if __name__ == "__main__":
    main()
