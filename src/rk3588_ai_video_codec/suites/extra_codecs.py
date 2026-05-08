from __future__ import annotations

import re
from collections.abc import Sequence
from pathlib import Path

from ..domain import format_case_name
from ..ffmpeg_backend import (
    FFMPEG_HW_UNAVAILABLE_RE,
    build_ffmpeg_generate_command,
    build_ffmpeg_hardware_decode_command,
    build_ffmpeg_software_decode_command,
    ffmpeg_has_codec,
    first_available_ffmpeg_codec,
    log_matches_unavailable,
)
from ..operations import (
    run_ffmpeg_decode,
    run_ffmpeg_encode,
    run_ffmpeg_generated_encode_to_file,
)
from ..process import calc_cpu_pct, calc_fps, calc_realtime, format_float, timed_run
from ..reporting import write_result
from ..runtime import BenchmarkContext


def run_vp8_tests(context: BenchmarkContext) -> None:
    codec_label = "VP8"
    ffmpeg_codec = "vp8_rkmpp"
    size = f"{context.settings.av1_width}x{context.settings.av1_height}"
    rate = 30
    frames = context.settings.av1_frames
    case_name = format_case_name(size, rate)
    artifact = context.paths.artifact_dir / f"vp8_probe_{context.config.profile}.webm"

    decode_available = ffmpeg_has_codec("decoders", ffmpeg_codec)
    if not decode_available:
        write_result(
            context,
            "UNAVAILABLE",
            codec_label,
            "decode",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"未发现 {ffmpeg_codec} 硬解码器",
            backend="ffmpeg",
        )

    encoder = run_ffmpeg_generated_encode_to_file(
        context,
        codec_label,
        case_name,
        size,
        rate,
        frames,
        artifact,
        encoder_candidates=("vp8_rkmpp", "vp8_v4l2m2m"),
        muxer="webm",
        bitrate="800k" if context.settings.av1_height <= 360 else "2000k",
        pixel_format="nv12",
    )
    if decode_available and encoder is None:
        write_result(
            context,
            "UNAVAILABLE",
            codec_label,
            "decode",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            "未能生成 FFmpeg VP8 样本，无法执行 vp8_rkmpp 解码探测",
            str(artifact),
            backend="ffmpeg",
        )
    elif decode_available and artifact.exists():
        run_ffmpeg_decode(
            context,
            codec_label,
            ffmpeg_codec,
            case_name,
            size,
            rate,
            frames,
            0,
            artifact,
        )


def run_generated_decode_probe(
    context: BenchmarkContext,
    codec_label: str,
    ffmpeg_decoder: str,
    case_name: str,
    size: str,
    rate: int,
    frames: int,
    sample: Path,
    *,
    encoder_candidates: Sequence[str],
    muxer: str,
    bitrate: str,
    pixel_format: str,
    extra_options: Sequence[str] = (),
    hardware_failure_re: re.Pattern[str] | None = None,
) -> None:
    if not ffmpeg_has_codec("decoders", ffmpeg_decoder):
        write_result(
            context,
            "UNAVAILABLE",
            codec_label,
            "decode",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"未发现 {ffmpeg_decoder} 硬解码器",
            backend="ffmpeg",
        )
        return

    encoder = first_available_ffmpeg_codec("encoders", encoder_candidates)
    if encoder is None:
        write_result(
            context,
            "UNAVAILABLE",
            codec_label,
            "decode",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"未发现可用于生成 {codec_label} 样本的 ffmpeg 编码器",
            str(sample),
            backend="ffmpeg",
        )
        return

    slug = codec_label.lower()
    profile = context.config.profile
    generate_log = context.paths.log_dir / f"{slug}_generate_{profile}.log"
    software_log = context.paths.log_dir / f"{slug}_software_decode_{profile}.log"
    hardware_log = context.paths.log_dir / f"{slug}_hardware_decode_{profile}.log"

    generate_command = build_ffmpeg_generate_command(
        sample,
        size,
        rate,
        frames,
        encoder=encoder,
        muxer=muxer,
        pixel_format=pixel_format,
        bitrate=bitrate,
        extra_options=extra_options,
    )
    generate_result = timed_run(generate_command, generate_log, cwd=context.config.repo_root)
    generate_text = generate_log.read_text(encoding="utf-8", errors="ignore")
    if log_matches_unavailable(generate_text, hardware_failure_re):
        write_result(
            context,
            "UNAVAILABLE",
            codec_label,
            "decode",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"ffmpeg {encoder} 当前环境不可用，详见 logs/{generate_log.name}",
            str(sample),
            backend="ffmpeg",
        )
        return
    if generate_result.returncode != 0:
        write_result(
            context,
            "FAIL",
            codec_label,
            "decode",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"{codec_label} 样本生成失败，详见 logs/{generate_log.name}",
            str(sample),
            backend="ffmpeg",
        )
        return

    software_command = build_ffmpeg_software_decode_command(sample, frames)
    software_result = timed_run(software_command, software_log, cwd=context.config.repo_root)
    if software_result.returncode != 0:
        write_result(
            context,
            "FAIL",
            codec_label,
            "decode",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"{codec_label} 软件解码基线失败，样本不可用，详见 logs/{software_log.name}",
            str(sample),
            backend="ffmpeg",
        )
        return

    hardware_command = build_ffmpeg_hardware_decode_command(sample, frames, ffmpeg_decoder)
    hardware_result = timed_run(hardware_command, hardware_log, cwd=context.config.repo_root)
    hardware_text = hardware_log.read_text(encoding="utf-8", errors="ignore")
    if log_matches_unavailable(hardware_text, hardware_failure_re):
        write_result(
            context,
            "UNAVAILABLE",
            codec_label,
            "decode",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"{ffmpeg_decoder} 当前环境不可用，详见 logs/{hardware_log.name}",
            str(sample),
            backend="ffmpeg",
        )
        return

    if hardware_result.returncode != 0:
        write_result(
            context,
            "FAIL",
            codec_label,
            "decode",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"{ffmpeg_decoder} 返回非 0，详见 logs/{hardware_log.name}",
            str(sample),
            backend="ffmpeg",
        )
        return

    fps = calc_fps(frames, hardware_result.elapsed)
    write_result(
        context,
        "PASS",
        codec_label,
        "decode",
        case_name,
        size,
        str(rate),
        str(frames),
        format_float(hardware_result.elapsed),
        fps,
        calc_realtime(fps, rate),
        calc_cpu_pct(hardware_result.user, hardware_result.sys, hardware_result.elapsed),
        f"sample_encoder={encoder} decoder={ffmpeg_decoder}",
        str(sample),
        backend="ffmpeg",
    )


def run_vp9_tests(context: BenchmarkContext) -> None:
    codec_label = "VP9"
    ffmpeg_decoder = "vp9_rkmpp"
    size = f"{context.settings.av1_width}x{context.settings.av1_height}"
    rate = 30
    frames = context.settings.av1_frames
    case_name = format_case_name(size, rate)
    sample = context.paths.artifact_dir / f"vp9_probe_{context.config.profile}.webm"

    run_generated_decode_probe(
        context,
        codec_label,
        ffmpeg_decoder,
        case_name,
        size,
        rate,
        frames,
        sample,
        encoder_candidates=("vp9_rkmpp", "libvpx-vp9", "vp9_v4l2m2m"),
        muxer="webm",
        bitrate="2000k",
        pixel_format="yuv420p",
    )


def run_extra_codec_suites(context: BenchmarkContext) -> None:
    run_vp8_tests(context)
    run_vp9_tests(context)


def run_av1_tests(context: BenchmarkContext) -> None:
    size = f"{context.settings.av1_width}x{context.settings.av1_height}"
    rate = 30
    case_name = format_case_name(size, rate)
    sample = context.paths.artifact_dir / f"av1_probe_{context.config.profile}.webm"
    run_generated_decode_probe(
        context,
        "AV1",
        "av1_rkmpp",
        case_name,
        size,
        rate,
        context.settings.av1_frames,
        sample,
        encoder_candidates=("av1_rkmpp", "libsvtav1", "librav1e", "libaom-av1"),
        muxer="webm",
        bitrate="1200k",
        pixel_format="yuv420p",
        hardware_failure_re=FFMPEG_HW_UNAVAILABLE_RE,
    )
