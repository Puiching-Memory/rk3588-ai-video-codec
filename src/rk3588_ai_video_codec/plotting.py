from __future__ import annotations

import csv
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .benchmark import BenchmarkError
from .ffmpeg_backend import FFMPEG_BIN

NOTE_PAIR_RE = re.compile(r"([A-Za-z0-9_]+)=([^\s]+)")
CASE_KBPS_RE = re.compile(r"_(\d+)kbps$", re.IGNORECASE)


@dataclass(frozen=True)
class SummaryPoint:
    codec: str
    result_type: str
    backend: str | None
    case_name: str
    size: str
    rate: int | None
    frames: int | None
    note: str
    artifact: Path | None
    source_path: Path | None
    bpp: float | None
    target_bpp: float | None
    psnr_avg: float | None
    ssim_all: float | None
    ssim_db: float | None
    fps: float | None
    realtime: float | None
    latency_ms: float | None
    cpu_pct: float | None


def resolve_summary_path(path: Path) -> Path:
    summary_path = path
    if summary_path.is_dir():
        summary_path = summary_path / "summary.tsv"
    if not summary_path.exists():
        raise BenchmarkError(f"未找到 summary.tsv: {summary_path}")
    return summary_path


def parse_note_metrics(note: str) -> dict[str, str]:
    return {key: value for key, value in NOTE_PAIR_RE.findall(note)}


def parse_float(value: str | None) -> float | None:
    if value is None or value == "" or value == "n/a":
        return None
    if value == "inf":
        return float("inf")
    return float(value)


def parse_percent(value: str | None) -> float | None:
    if value is None or value == "" or value == "n/a":
        return None
    return float(value.rstrip("%"))


def parse_int(value: str | None) -> int | None:
    if value is None or value == "" or value == "n/a":
        return None
    return int(value)


def parse_bpp(value: str | None) -> float | None:
    if value is None or value == "":
        return None
    return float(value)


def resolve_artifact_path(value: str | None, base_dir: Path) -> Path | None:
    if value is None or value == "":
        return None
    artifact = Path(value)
    if not artifact.is_absolute():
        artifact = base_dir / artifact
    return artifact


def parse_case_kbps(case_name: str) -> int | None:
    match = CASE_KBPS_RE.search(case_name)
    if not match:
        return None
    return int(match.group(1))


def infer_backend(codec: str, result_type: str, note: str, backend: str | None) -> str | None:
    if backend:
        return backend

    if result_type in {"stream-pipeline", "encode", "decode", "quality", "suite"}:
        return "ffmpeg"

    if codec in {"H264", "H265", "VP8", "VP9", "AV1"}:
        return "ffmpeg"

    if "encoder=" in note or "decoder=" in note:
        return "ffmpeg"

    return None


def load_summary_points(
    summary_path: Path,
    source_path: str | None = None,
) -> list[SummaryPoint]:
    resolved_summary = resolve_summary_path(summary_path)
    points: list[SummaryPoint] = []
    with resolved_summary.open("r", encoding="utf-8") as file:
        reader = csv.DictReader(file, delimiter="\t")
        for row in reader:
            if row.get("status") != "PASS":
                continue

            note_metrics = parse_note_metrics(row.get("note", ""))
            bpp = parse_float(note_metrics.get("avg_bpp"))

            rate = parse_int(row.get("rate"))
            frames = parse_int(row.get("frames"))
            elapsed_s = parse_float(row.get("elapsed_s"))
            latency_ms = None
            if frames and elapsed_s is not None:
                latency_ms = (elapsed_s / frames) * 1000.0

            points.append(
                SummaryPoint(
                    codec=row.get("codec", ""),
                    result_type=row.get("type", ""),
                    backend=infer_backend(
                        row.get("codec", ""),
                        row.get("type", ""),
                        row.get("note", ""),
                        row.get("backend"),
                    ),
                    case_name=row.get("case", ""),
                    size=row.get("size", ""),
                    rate=rate,
                    frames=frames,
                    note=row.get("note", ""),
                    artifact=resolve_artifact_path(row.get("artifact"), resolved_summary.parent),
                    source_path=Path(source_path) if source_path else None,
                    bpp=bpp,
                    target_bpp=parse_float(note_metrics.get("target_bpp")),
                    psnr_avg=parse_float(note_metrics.get("psnr_avg")),
                    ssim_all=parse_float(note_metrics.get("ssim_all")),
                    ssim_db=parse_float(note_metrics.get("ssim_db")),
                    fps=parse_float(row.get("fps")),
                    realtime=parse_float(row.get("realtime")),
                    latency_ms=latency_ms,
                    cpu_pct=parse_percent(row.get("cpu")),
                )
            )
    return points


def select_quality_points(points: list[SummaryPoint]) -> list[SummaryPoint]:
    return [
        point
        for point in points
        if point.result_type == "quality"
        and (point.bpp is not None or point.target_bpp is not None)
    ]


def select_runtime_points(points: list[SummaryPoint]) -> list[SummaryPoint]:
    runtime_points = [
        point
        for point in points
        if (point.bpp is not None or point.target_bpp is not None)
        and (point.fps is not None or point.latency_ms is not None)
    ]
    if runtime_points:
        return runtime_points

    quality_points = select_quality_points(points)
    if quality_points:
        return quality_points

    raise BenchmarkError("summary.tsv 中没有可用于绘图的 bpp 数据")


def select_plot_points(points: list[SummaryPoint]) -> list[SummaryPoint]:
    quality_points = select_quality_points(points)
    if quality_points:
        return quality_points
    return select_runtime_points(points)


def select_preview_points(points: list[SummaryPoint]) -> list[SummaryPoint]:
    source_points = select_quality_points(points)
    if not source_points:
        source_points = [p for p in points if p.result_type == "encode"]
    if not source_points:
        source_points = [p for p in points if p.artifact is not None]

    grouped: dict[str, list[SummaryPoint]] = {}
    for point in source_points:
        grouped.setdefault(point.codec, []).append(point)

    preview_points: list[SummaryPoint] = []
    for codec in sorted(grouped):
        codec_points = grouped[codec]
        preview_points.append(
            min(
                codec_points,
                key=lambda point: (
                    parse_case_kbps(point.case_name) != 100,
                    parse_case_kbps(point.case_name) or 10**9,
                    "2160" in point.case_name,
                    point.result_type != "encode",
                    plot_bpp(point) or float("inf"),
                ),
            )
        )
    return preview_points


def series_label(point: SummaryPoint, include_type: bool) -> str:
    backend_suffix = f" ({point.backend})" if point.backend else ""
    if include_type:
        return f"{point.codec} {point.result_type}{backend_suffix}"
    return f"{point.codec}{backend_suffix}"


def plot_bpp(point: SummaryPoint) -> float | None:
    if point.target_bpp is not None:
        return point.target_bpp
    return point.bpp


def build_series(points: list[SummaryPoint]) -> dict[str, list[SummaryPoint]]:
    include_type = len({point.result_type for point in points}) > 1
    grouped: dict[str, list[SummaryPoint]] = {}
    for point in points:
        label = series_label(point, include_type)
        grouped.setdefault(label, []).append(point)

    for label in grouped:
        grouped[label].sort(key=lambda point: plot_bpp(point) or 0.0)
    return grouped


def load_pyplot() -> Any:
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as pyplot
    except ImportError as error:
        raise BenchmarkError("缺少 matplotlib，请先执行 uv sync") from error
    return pyplot


def draw_metric(
    ax: Any,
    series: dict[str, list[SummaryPoint]],
    metric_name: str,
    y_label: str,
) -> None:
    plotted = False
    for label, points in series.items():
        x_values: list[float] = []
        y_values: list[float] = []
        for point in points:
            x_value = plot_bpp(point)
            y_value = getattr(point, metric_name)
            if x_value is None or y_value is None:
                continue
            x_values.append(x_value)
            y_values.append(y_value)

        if not x_values:
            continue

        ax.plot(x_values, y_values, marker="o", linewidth=2, label=label)
        plotted = True

    ax.set_xlabel("Bits per pixel [bpp]")
    ax.set_ylabel(y_label)
    ax.grid(True, alpha=0.35)
    if plotted:
        ax.legend()
    else:
        ax.text(0.5, 0.5, "No data", ha="center", va="center", transform=ax.transAxes)


def render_metric_chart(
    pyplot: Any,
    series: dict[str, list[SummaryPoint]],
    out_path: Path,
    title: str,
    metric_name: str,
    y_label: str,
) -> Path:
    figure, axis = pyplot.subplots(figsize=(9.5, 6.0), constrained_layout=True)
    draw_metric(axis, series, metric_name, y_label)
    axis.set_title(title)
    figure.savefig(out_path, dpi=180)
    pyplot.close(figure)
    return out_path


def build_preview_filter(size: str, frame_index: int) -> str:
    crop_size = size.replace("x", ":")
    end_frame = frame_index + 1
    return (
        f"[0:v]trim=start_frame={frame_index}:end_frame={end_frame},"
        f"setpts=PTS-STARTPTS,settb=AVTB,format=yuv420p,crop={crop_size},setsar=1[left];"
        f"[1:v]trim=start_frame={frame_index}:end_frame={end_frame},"
        f"setpts=PTS-STARTPTS,settb=AVTB,format=yuv420p,crop={crop_size},setsar=1[right];"
        "[left][right]hstack=inputs=2"
    )


def render_preview_image(point: SummaryPoint, out_dir: Path) -> Path | None:
    if point.artifact is None or not point.artifact.exists() or point.rate is None:
        return None

    out_path = out_dir / f"preview_{point.codec.lower()}_{point.case_name}.png"

    if point.source_path is not None and point.source_path.exists():
        source_input = ["-i", str(point.source_path)]
        # Use frame 0 for file sources (we don't know exact frame count)
        frame_index = 0
    else:
        source_input = [
            "-f", "lavfi",
            "-i", f"testsrc2=size={point.size}:rate={point.rate}",
        ]
        frame_index = 0
        if point.frames is not None and point.frames > 1:
            frame_index = min(point.frames // 2, point.frames - 1)

    command = [
        FFMPEG_BIN,
        "-hide_banner",
        "-loglevel",
        "error",
        *source_input,
        "-i",
        str(point.artifact),
        "-filter_complex",
        build_preview_filter(point.size, frame_index),
        "-frames:v",
        "1",
        "-update",
        "1",
        "-y",
        str(out_path),
    ]
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        return None
    return out_path


def render_preview_images(points: list[SummaryPoint], out_dir: Path) -> list[Path]:
    output_paths: list[Path] = []
    for point in select_preview_points(points):
        preview_path = render_preview_image(point, out_dir)
        if preview_path is not None:
            output_paths.append(preview_path)
    return output_paths


def render_dashboard(
    pyplot: Any,
    quality_series: dict[str, list[SummaryPoint]],
    runtime_series: dict[str, list[SummaryPoint]],
    out_path: Path,
    title: str,
) -> Path:
    figure, axes = pyplot.subplots(2, 2, figsize=(14, 10), constrained_layout=True)
    draw_metric(axes[0][0], quality_series, "psnr_avg", "PSNR [dB]")
    axes[0][0].set_title("BPP vs PSNR")
    draw_metric(axes[0][1], quality_series, "ssim_all", "SSIM")
    axes[0][1].set_title("BPP vs SSIM")
    draw_metric(axes[1][0], runtime_series, "fps", "Throughput [fps]")
    axes[1][0].set_title("BPP vs Throughput")
    draw_metric(axes[1][1], runtime_series, "latency_ms", "Latency [ms/frame]")
    axes[1][1].set_title("BPP vs Latency")
    figure.suptitle(title)
    figure.savefig(out_path, dpi=180)
    pyplot.close(figure)
    return out_path


def render_summary_plots(
    summary_path: Path,
    *,
    out_dir: Path | None = None,
    title: str | None = None,
    source_path: str | None = None,
) -> list[Path]:
    resolved_summary = resolve_summary_path(summary_path)
    all_points = load_summary_points(resolved_summary, source_path=source_path)
    quality_points = select_quality_points(all_points)
    runtime_points = select_runtime_points(all_points)
    quality_series = build_series(quality_points) if quality_points else {}
    runtime_series = build_series(runtime_points)
    pyplot = load_pyplot()

    if out_dir is None:
        out_dir = resolved_summary.parent / "plots"
    out_dir.mkdir(parents=True, exist_ok=True)

    plot_title = title or f"RK3588 Codec Metrics - {resolved_summary.parent.name}"
    output_paths = [
        render_dashboard(
            pyplot,
            quality_series,
            runtime_series,
            out_dir / "rd_performance_latency_dashboard.png",
            plot_title,
        ),
        render_metric_chart(
            pyplot,
            quality_series,
            out_dir / "bpp_vs_psnr.png",
            f"{plot_title} - PSNR",
            "psnr_avg",
            "PSNR [dB]",
        ),
        render_metric_chart(
            pyplot,
            quality_series,
            out_dir / "bpp_vs_ssim.png",
            f"{plot_title} - SSIM",
            "ssim_all",
            "SSIM",
        ),
        render_metric_chart(
            pyplot,
            runtime_series,
            out_dir / "bpp_vs_fps.png",
            f"{plot_title} - Throughput",
            "fps",
            "Throughput [fps]",
        ),
        render_metric_chart(
            pyplot,
            runtime_series,
            out_dir / "bpp_vs_latency_ms.png",
            f"{plot_title} - Latency",
            "latency_ms",
            "Latency [ms/frame]",
        ),
    ]
    output_paths.extend(render_preview_images(all_points, out_dir))
    return output_paths
