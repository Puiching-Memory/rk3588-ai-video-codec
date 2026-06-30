#!/usr/bin/env python3
"""绘制端到端编解码性能图（编码/解码速度 fps）。"""

from __future__ import annotations

import argparse
import csv
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from plot_rd_curve import (
    codec_color,
    codec_label,
    is_upscale_group,
    sort_codecs,
    upscale_group_color,
    upscale_group_key,
)


def _span(stats_list: list[dict], key: str) -> tuple[float, float, float]:
    vals = [s[key] for s in stats_list]
    med = statistics.median(vals)
    return med, min(vals), max(vals)


def merge_upscale_perf(data: dict[str, dict[str, float]]) -> dict[str, dict[str, float]]:
    """将三种插值路线的性能中位数合并为一条，附带 min–max 范围。"""
    groups: dict[str, list[dict[str, float]]] = defaultdict(list)
    out: dict[str, dict[str, float]] = {}

    def _with_identity_range(stats: dict[str, float]) -> dict[str, float]:
        row = dict(stats)
        for key in ("enc_fps", "dec_fps", "post_fps", "e2e_fps"):
            row[f"{key}_lo"] = stats.get(f"{key}_lo", stats[key])
            row[f"{key}_hi"] = stats.get(f"{key}_hi", stats[key])
        return row

    for codec, stats in data.items():
        gk = upscale_group_key(codec)
        if gk:
            groups[gk].append(stats)
        else:
            out[codec] = _with_identity_range(stats)

    for gk, items in groups.items():
        enc_m, enc_lo, enc_hi = _span(items, "enc_fps")
        dec_m, dec_lo, dec_hi = _span(items, "dec_fps")
        post_m, post_lo, post_hi = _span(items, "post_fps")
        e2e_m, e2e_lo, e2e_hi = _span(items, "e2e_fps")
        out[gk] = {
            "enc_fps": enc_m,
            "enc_fps_lo": enc_lo,
            "enc_fps_hi": enc_hi,
            "dec_fps": dec_m,
            "dec_fps_lo": dec_lo,
            "dec_fps_hi": dec_hi,
            "post_fps": post_m,
            "post_fps_lo": post_lo,
            "post_fps_hi": post_hi,
            "e2e_fps": e2e_m,
            "e2e_fps_lo": e2e_lo,
            "e2e_fps_hi": e2e_hi,
        }
    return out


def _perf_color(codec: str) -> str:
    return upscale_group_color(codec) if is_upscale_group(codec) else (codec_color(codec) or "#888888")


def _yerr(med: float, lo: float, hi: float) -> tuple[list[float], list[float]]:
    return [max(0.0, med - lo)], [max(0.0, hi - med)]


def load_perf(path: Path, frames: int) -> dict[str, dict[str, float]]:
    enc_fps: dict[str, list[float]] = defaultdict(list)
    dec_fps: dict[str, list[float]] = defaultdict(list)
    post_fps: dict[str, list[float]] = defaultdict(list)
    enc_sec: dict[str, list[float]] = defaultdict(list)
    dec_sec: dict[str, list[float]] = defaultdict(list)
    post_sec: dict[str, list[float]] = defaultdict(list)
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            codec = row["codec"]
            enc_s = float(row["encode_sec"])
            dec_s = float(row["decode_sec"])
            post_s = float(row.get("postproc_sec") or 0)
            enc_sec[codec].append(enc_s)
            dec_sec[codec].append(dec_s)
            post_sec[codec].append(post_s)
            if enc_s > 0:
                enc_fps[codec].append(frames / enc_s)
            if dec_s > 0:
                dec_fps[codec].append(frames / dec_s)
            if post_s > 0:
                post_fps[codec].append(frames / post_s)

    out: dict[str, dict[str, float]] = {}
    for codec in set(enc_fps) | set(dec_fps) | set(post_fps):
        med_enc_s = statistics.median(enc_sec[codec])
        med_dec_s = statistics.median(dec_sec[codec])
        med_post_s = statistics.median(post_sec[codec]) if post_sec[codec] else 0.0
        med_enc_fps = statistics.median(enc_fps[codec])
        med_dec_fps = statistics.median(dec_fps[codec])
        med_post_fps = statistics.median(post_fps[codec]) if post_fps[codec] else 0.0
        total = med_enc_s + med_dec_s + med_post_s
        out[codec] = {
            "enc_fps": med_enc_fps,
            "dec_fps": med_dec_fps,
            "post_fps": med_post_fps,
            "e2e_fps": frames / total if total > 0 else 0.0,
        }
    return out


def plot_perf(
    data: dict[str, dict[str, float]],
    out_prefix: Path,
    title: str,
    realtime_fps: float,
) -> None:
    data = merge_upscale_perf(data)

    plt.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "axes.unicode_minus": False,
            "figure.dpi": 150,
        }
    )

    codecs = sort_codecs(list(data.keys()))
    labels = [codec_label(c) for c in codecs]
    colors = [_perf_color(c) for c in codecs]
    x = np.arange(len(codecs))
    width = 0.26

    def _vals(key: str) -> list[float]:
        return [data[c][key] for c in codecs]

    def _errs(lo_key: str, hi_key: str, med_key: str) -> tuple[np.ndarray, np.ndarray]:
        low = np.array([data[c][med_key] - data[c][lo_key] for c in codecs])
        high = np.array([data[c][hi_key] - data[c][med_key] for c in codecs])
        return low, high

    enc = _vals("enc_fps")
    dec = _vals("dec_fps")
    post = [data[c].get("post_fps", 0.0) for c in codecs]
    e2e = _vals("e2e_fps")
    has_post = any(v > 0 for v in post)
    has_upscale_range = any(is_upscale_group(c) for c in codecs)

    def _maybe_err(lo_key: str, hi_key: str, med_key: str) -> tuple[np.ndarray, np.ndarray] | None:
        if not has_upscale_range:
            return None
        return _errs(lo_key, hi_key, med_key)

    fig, axes = plt.subplots(1, 2, figsize=(12, 5))

    ax = axes[0]
    err_kw = dict(capsize=2, capthick=0.8, elinewidth=0.8, ecolor="#444444")
    if has_post:
        err = _maybe_err("enc_fps_lo", "enc_fps_hi", "enc_fps")
        ax.bar(x - 1.5 * width, enc, width, label="Encode", color=colors, alpha=0.92, edgecolor="white", yerr=err, error_kw=err_kw)
        err = _maybe_err("dec_fps_lo", "dec_fps_hi", "dec_fps")
        ax.bar(x - 0.5 * width, dec, width, label="Decode", color=colors, alpha=0.55, edgecolor="white", hatch="//", yerr=err, error_kw=err_kw)
        if any(post_mask := [v > 0 for v in post]):
            err = _maybe_err("post_fps_lo", "post_fps_hi", "post_fps")
            ax.bar(x + 0.5 * width, post, width, label="Post-upscale", color=colors, alpha=0.45, edgecolor="white", hatch="xx", yerr=err, error_kw=err_kw)
        err = _maybe_err("e2e_fps_lo", "e2e_fps_hi", "e2e_fps")
        ax.bar(x + 1.5 * width, e2e, width, label="E2E", color=colors, alpha=0.35, edgecolor="white", hatch="..", yerr=err, error_kw=err_kw)
    else:
        err = _maybe_err("enc_fps_lo", "enc_fps_hi", "enc_fps")
        ax.bar(x - width, enc, width, label="Encode", color=colors, alpha=0.92, edgecolor="white", yerr=err, error_kw=err_kw)
        err = _maybe_err("dec_fps_lo", "dec_fps_hi", "dec_fps")
        ax.bar(x, dec, width, label="Decode", color=colors, alpha=0.55, edgecolor="white", hatch="//", yerr=err, error_kw=err_kw)
        err = _maybe_err("e2e_fps_lo", "e2e_fps_hi", "e2e_fps")
        ax.bar(x + width, e2e, width, label="E2E", color=colors, alpha=0.35, edgecolor="white", hatch="..", yerr=err, error_kw=err_kw)
    ax.axhline(realtime_fps, color="#c44e52", linestyle="--", linewidth=1.2,
               label=f"Realtime ({realtime_fps:.0f} fps)")
    ax.set_yscale("log")
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=15, ha="right")
    ax.set_ylabel("Throughput (fps, log scale)")
    ax.set_title("Encode / Decode / E2E Speed")
    ax.grid(True, axis="y", linestyle="--", alpha=0.4)
    ax.legend(loc="upper right", fontsize=8, framealpha=0.9)

    for i, c in enumerate(codecs):
        if has_post:
            ax.text(i - 1.5 * width, enc[i] * 1.08, f"{enc[i]:.1f}", ha="center", va="bottom", fontsize=7)
            ax.text(i - 0.5 * width, dec[i] * 1.08, f"{dec[i]:.1f}", ha="center", va="bottom", fontsize=7)
            if post[i] > 0:
                ax.text(i + 0.5 * width, post[i] * 1.08, f"{post[i]:.1f}", ha="center", va="bottom", fontsize=7)
            ax.text(i + 1.5 * width, e2e[i] * 1.08, f"{e2e[i]:.1f}", ha="center", va="bottom", fontsize=7)
        else:
            ax.text(i - width, enc[i] * 1.08, f"{enc[i]:.1f}", ha="center", va="bottom", fontsize=7)
            ax.text(i, dec[i] * 1.08, f"{dec[i]:.1f}", ha="center", va="bottom", fontsize=7)
            ax.text(i + width, e2e[i] * 1.08, f"{e2e[i]:.1f}", ha="center", va="bottom", fontsize=7)

    ax2 = axes[1]
    rt_enc = [realtime_fps / v if v > 0 else float("inf") for v in enc]
    rt_dec = [realtime_fps / v if v > 0 else float("inf") for v in dec]
    rt_post = [realtime_fps / v if v > 0 else 0.0 for v in post]
    rt_e2e = [realtime_fps / v if v > 0 else float("inf") for v in e2e]

    def _rt_err(lo_key: str, hi_key: str, med_key: str) -> tuple[np.ndarray, np.ndarray]:
        low, high = [], []
        for c in codecs:
            med = data[c][med_key]
            lo = data[c][lo_key]
            hi = data[c][hi_key]
            if med <= 0:
                low.append(0.0)
                high.append(0.0)
                continue
            low.append(max(0.0, realtime_fps / hi - realtime_fps / med))
            high.append(max(0.0, realtime_fps / lo - realtime_fps / med))
        return np.array(low), np.array(high)

    if has_post:
        el, eh = _rt_err("enc_fps_lo", "enc_fps_hi", "enc_fps")
        ax2.bar(x - 1.5 * width, rt_enc, width, label="Encode", color=colors, alpha=0.92, edgecolor="white", yerr=[el, eh], error_kw=err_kw)
        el, eh = _rt_err("dec_fps_lo", "dec_fps_hi", "dec_fps")
        ax2.bar(x - 0.5 * width, rt_dec, width, label="Decode", color=colors, alpha=0.55, edgecolor="white", hatch="//", yerr=[el, eh], error_kw=err_kw)
        if any(v > 0 for v in post):
            el, eh = _rt_err("post_fps_lo", "post_fps_hi", "post_fps")
            ax2.bar(x + 0.5 * width, rt_post, width, label="Post-upscale", color=colors, alpha=0.45, edgecolor="white", hatch="xx", yerr=[el, eh], error_kw=err_kw)
        el, eh = _rt_err("e2e_fps_lo", "e2e_fps_hi", "e2e_fps")
        ax2.bar(x + 1.5 * width, rt_e2e, width, label="E2E", color=colors, alpha=0.35, edgecolor="white", hatch="..", yerr=[el, eh], error_kw=err_kw)
    else:
        el, eh = _rt_err("enc_fps_lo", "enc_fps_hi", "enc_fps") if has_upscale_range else (np.zeros(len(codecs)), np.zeros(len(codecs)))
        ax2.bar(x - width, rt_enc, width, label="Encode", color=colors, alpha=0.92, edgecolor="white", yerr=[el, eh] if has_upscale_range else None, error_kw=err_kw)
        el, eh = _rt_err("dec_fps_lo", "dec_fps_hi", "dec_fps") if has_upscale_range else (np.zeros(len(codecs)), np.zeros(len(codecs)))
        ax2.bar(x, rt_dec, width, label="Decode", color=colors, alpha=0.55, edgecolor="white", hatch="//", yerr=[el, eh] if has_upscale_range else None, error_kw=err_kw)
        el, eh = _rt_err("e2e_fps_lo", "e2e_fps_hi", "e2e_fps") if has_upscale_range else (np.zeros(len(codecs)), np.zeros(len(codecs)))
        ax2.bar(x + width, rt_e2e, width, label="E2E", color=colors, alpha=0.35, edgecolor="white", hatch="..", yerr=[el, eh] if has_upscale_range else None, error_kw=err_kw)
    ax2.axhline(1.0, color="#c44e52", linestyle="--", linewidth=1.2, label="Realtime (=1×)")
    ax2.set_yscale("log")
    ax2.set_xticks(x)
    ax2.set_xticklabels(labels, rotation=15, ha="right")
    ax2.set_ylabel("Slower than realtime (×, log scale)")
    ax2.set_title("Realtime Factor (>1 = slower than realtime)")
    ax2.grid(True, axis="y", linestyle="--", alpha=0.4)
    ax2.legend(loc="upper left", fontsize=8, framealpha=0.9)

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
    parser = argparse.ArgumentParser(description="绘制编解码性能图")
    parser.add_argument("--csv", type=Path, default=bench_root / "results" / "rd_data.csv")
    parser.add_argument("--out", type=Path, default=bench_root / "results" / "perf_e2e")
    parser.add_argument("--frames", type=int, default=62, help="测试片段帧数")
    parser.add_argument("--realtime-fps", type=float, default=30.0)
    parser.add_argument(
        "--title",
        default="E2E Performance (RK3588, median over rate points)",
    )
    args = parser.parse_args()

    if not args.csv.exists():
        raise SystemExit(f"找不到数据文件: {args.csv}")

    data = load_perf(args.csv, args.frames)
    if not data:
        raise SystemExit("CSV 无有效性能数据")
    plot_perf(data, args.out, args.title, args.realtime_fps)


if __name__ == "__main__":
    main()
