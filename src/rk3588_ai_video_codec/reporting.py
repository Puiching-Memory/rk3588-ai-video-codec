from __future__ import annotations

import os
from datetime import datetime
from pathlib import Path

from .ffmpeg_backend import FFMPEG_BIN
from .process import ResultRow, capture_command, command_exists, sanitize_field
from .runtime import BenchmarkContext


def write_result(
    context: BenchmarkContext,
    status: str,
    codec: str,
    type_: str,
    case_name: str,
    size: str,
    rate: str,
    frames: str,
    elapsed: str,
    fps: str,
    realtime: str,
    cpu: str,
    note: str = "",
    artifact: str = "",
    backend: str = "",
) -> None:
    row = ResultRow(
        status=status,
        codec=codec,
        type=type_,
        backend=sanitize_field(backend),
        case_name=case_name,
        size=size,
        rate=rate,
        frames=frames,
        elapsed=elapsed,
        fps=fps,
        realtime=realtime,
        cpu=cpu,
        note=sanitize_field(note),
        artifact=sanitize_field(artifact),
    )
    context.state.rows.append(row)
    with context.paths.summary_tsv.open("a", encoding="utf-8") as file:
        file.write(row.to_tsv() + "\n")

    if status == "FAIL":
        context.state.fail_count += 1
    elif status == "UNAVAILABLE":
        context.state.unavailable_count += 1

    suffix = f" {row.note}" if row.note else ""
    backend_prefix = f"[{row.backend}] " if row.backend else ""
    print(f"[{status}] {backend_prefix}{codec} {type_} {case_name}{suffix}")


def write_system_info(context: BenchmarkContext) -> None:
    compatible_path = Path("/proc/device-tree/compatible")
    compatibles = []
    if compatible_path.exists():
        compatibles = [
            item.decode("utf-8", errors="ignore")
            for item in compatible_path.read_bytes().split(b"\0")
            if item
        ]

    cpu_model = ""
    cpuinfo_path = Path("/proc/cpuinfo")
    if cpuinfo_path.exists():
        for line in cpuinfo_path.read_text(encoding="utf-8", errors="ignore").splitlines():
            if line.startswith("Model"):
                cpu_model = line
                break

    config = context.config
    lines = [
        f"timestamp={datetime.now().astimezone().isoformat()}",
        f"profile={config.profile}",
        f"out_dir={context.paths.out_dir}",
        f"ffmpeg_bin={FFMPEG_BIN}",
        f"throughput={config.run_throughput}",
        f"quality={config.run_quality}",
        f"extra_codecs={config.run_extra_codecs}",
        f"extended_quality={config.run_extended_quality}",
        "",
        "[uname]",
        capture_command(["uname", "-a"]),
        "",
        "[compatible]",
        "\n".join(compatibles),
        "",
        "[cpu]",
        str(os.cpu_count() or ""),
        cpu_model,
        "",
        "[memory]",
        capture_command(["free", "-h"]),
        "",
        "[ffmpeg-version]",
        "\n".join(capture_command([FFMPEG_BIN, "-hide_banner", "-version"]).splitlines()[:4]),
        "",
        "[ffmpeg-hwaccels]",
        capture_command([FFMPEG_BIN, "-hide_banner", "-hwaccels"]),
        "",
        "[v4l2-devices]",
    ]
    if command_exists("v4l2-ctl"):
        lines.append(capture_command(["v4l2-ctl", "--list-devices"]))
    else:
        lines.append("v4l2-ctl not found")

    context.paths.system_txt.write_text("\n".join(lines) + "\n", encoding="utf-8")


def render_summary_md(context: BenchmarkContext) -> None:
    lines = [
        "# RK3588 VPU Benchmark Summary",
        "",
        f"- Generated: {datetime.now().astimezone().isoformat()}",
        f"- Profile: {context.config.profile}",
        f"- Output: {context.paths.out_dir}",
        "",
        "| Status | Codec | Type | Backend | Case | FPS | Real-time | CPU | Note |",
        "| --- | --- | --- | --- | --- | ---: | ---: | ---: | --- |",
    ]
    for row in context.state.rows:
        lines.append(
            "| "
            f"{row.status} | {row.codec} | {row.type} | {row.backend} | {row.case_name} | "
            f"{row.fps} | {row.realtime} | {row.cpu} | {row.note} |"
        )
    lines.extend(["", "See system.txt, logs/, artifacts/, and summary.tsv for raw data."])
    context.paths.summary_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
