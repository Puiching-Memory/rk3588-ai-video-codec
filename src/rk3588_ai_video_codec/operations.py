from __future__ import annotations

import re
from collections.abc import Sequence
from pathlib import Path

from .domain import (
    QualityCase,
    build_quality_compare_filter,
    calc_avg_bpp,
    calc_target_bpp,
    parse_psnr_average,
    parse_size,
    parse_ssim_all,
)
from .ffmpeg_backend import (
    FFMPEG_BIN,
    build_ffmpeg_generate_command,
    build_ffmpeg_hardware_decode_command,
    first_available_ffmpeg_codec,
    log_matches_unavailable,
)
from .process import calc_cpu_pct, calc_fps, calc_realtime, format_float, timed_run
from .reporting import write_result
from .runtime import BenchmarkContext
from .test_sequences import build_source_input_args


def _source_args(context: BenchmarkContext, size: str, rate: int) -> list[str]:
    return build_source_input_args(context.config.source, size, rate)


def run_ffmpeg_encode(
    context: BenchmarkContext,
    codec_label: str,
    ffmpeg_codec: str,
    muxer: str,
    case_name: str,
    size: str,
    rate: int,
    frames: int,
    bitrate: str,
    artifact: Path,
) -> None:
    slug = codec_label.lower()
    log_path = context.paths.log_dir / f"{slug}_encode_{case_name}.log"
    command = [
        FFMPEG_BIN,
        "-hide_banner",
        "-loglevel",
        "error",
        *_source_args(context, size, rate),
        "-frames:v",
        str(frames),
        "-pix_fmt",
        "nv12",
        "-c:v",
        ffmpeg_codec,
        "-b:v",
        bitrate,
        "-f",
        muxer,
        "-y",
        str(artifact),
    ]
    result = timed_run(command, log_path, cwd=context.config.repo_root)
    if result.returncode != 0:
        write_result(
            context,
            "FAIL",
            codec_label,
            "encode",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"ffmpeg {ffmpeg_codec} 编码失败，详见 logs/{log_path.name}",
            str(artifact),
            backend="ffmpeg",
        )
        return

    bytes_written = artifact.stat().st_size
    width, height = parse_size(size)
    fps = calc_fps(frames, result.elapsed)
    write_result(
        context,
        "PASS",
        codec_label,
        "encode",
        case_name,
        size,
        str(rate),
        str(frames),
        format_float(result.elapsed),
        fps,
        calc_realtime(fps, rate),
        calc_cpu_pct(result.user, result.sys, result.elapsed),
        note=f"avg_bpp={calc_avg_bpp(bytes_written, frames, width, height)}",
        artifact=str(artifact),
        backend="ffmpeg",
    )


def run_ffmpeg_decode(
    context: BenchmarkContext,
    codec_label: str,
    ffmpeg_codec: str,
    case_name: str,
    size: str,
    rate: int,
    clip_frames: int,
    loops: int,
    artifact: Path,
) -> None:
    slug = codec_label.lower()
    total_frames = clip_frames * (loops + 1)
    log_path = context.paths.log_dir / f"{slug}_decode_{case_name}.log"
    command = [
        FFMPEG_BIN,
        "-hide_banner",
        "-loglevel",
        "error",
        "-stream_loop",
        str(loops),
        "-c:v",
        ffmpeg_codec,
        "-i",
        str(artifact),
        "-frames:v",
        str(total_frames),
        "-f",
        "null",
        "-",
    ]
    result = timed_run(command, log_path, cwd=context.config.repo_root)
    if result.returncode != 0:
        write_result(
            context,
            "FAIL",
            codec_label,
            "decode",
            case_name,
            size,
            str(rate),
            str(total_frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"ffmpeg {ffmpeg_codec} 解码失败，详见 logs/{log_path.name}",
            str(artifact),
            backend="ffmpeg",
        )
        return

    fps = calc_fps(total_frames, result.elapsed)
    write_result(
        context,
        "PASS",
        codec_label,
        "decode",
        case_name,
        size,
        str(rate),
        str(total_frames),
        format_float(result.elapsed),
        fps,
        calc_realtime(fps, rate),
        calc_cpu_pct(result.user, result.sys, result.elapsed),
        note=f"decoder={ffmpeg_codec}",
        artifact=str(artifact),
        backend="ffmpeg",
    )


def run_ffmpeg_quality(
    context: BenchmarkContext,
    codec_label: str,
    ffmpeg_codec: str,
    muxer: str,
    case: QualityCase,
    frames: int,
    artifact: Path,
) -> None:
    slug = codec_label.lower()
    encode_log = context.paths.log_dir / f"{slug}_quality_encode_{case.case_name}.log"
    psnr_log = context.paths.log_dir / f"{slug}_quality_psnr_{case.case_name}.log"
    ssim_log = context.paths.log_dir / f"{slug}_quality_ssim_{case.case_name}.log"

    encode_command = [
        FFMPEG_BIN,
        "-hide_banner",
        "-loglevel",
        "error",
        *_source_args(context, case.size, case.rate),
        "-frames:v",
        str(frames),
        "-pix_fmt",
        "nv12",
        "-c:v",
        ffmpeg_codec,
        "-b:v",
        case.bitrate,
        "-f",
        muxer,
        "-y",
        str(artifact),
    ]
    encode_result = timed_run(encode_command, encode_log, cwd=context.config.repo_root)
    if encode_result.returncode != 0:
        write_result(
            context,
            "FAIL",
            codec_label,
            "quality",
            case.case_name,
            case.size,
            str(case.rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"质量档位编码失败，详见 logs/{encode_log.name}",
            str(artifact),
            backend="ffmpeg",
        )
        return

    compare_base = [
        FFMPEG_BIN,
        "-hide_banner",
        "-loglevel",
        "info",
        *_source_args(context, case.size, case.rate),
        "-c:v",
        ffmpeg_codec,
        "-i",
        str(artifact),
    ]
    psnr_command = [
        *compare_base,
        "-filter_complex",
        build_quality_compare_filter(case.size, frames, "psnr"),
        "-frames:v",
        str(frames),
        "-f",
        "null",
        "-",
    ]
    psnr_result = timed_run(psnr_command, psnr_log, cwd=context.config.repo_root)
    if psnr_result.returncode != 0:
        write_result(
            context,
            "FAIL",
            codec_label,
            "quality",
            case.case_name,
            case.size,
            str(case.rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"PSNR 计算失败，详见 logs/{psnr_log.name}",
            str(artifact),
            backend="ffmpeg",
        )
        return

    ssim_command = [
        *compare_base,
        "-filter_complex",
        build_quality_compare_filter(case.size, frames, "ssim"),
        "-frames:v",
        str(frames),
        "-f",
        "null",
        "-",
    ]
    ssim_result = timed_run(ssim_command, ssim_log, cwd=context.config.repo_root)
    if ssim_result.returncode != 0:
        write_result(
            context,
            "FAIL",
            codec_label,
            "quality",
            case.case_name,
            case.size,
            str(case.rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"SSIM 计算失败，详见 logs/{ssim_log.name}",
            str(artifact),
            backend="ffmpeg",
        )
        return

    psnr_text = psnr_log.read_text(encoding="utf-8", errors="ignore")
    ssim_text = ssim_log.read_text(encoding="utf-8", errors="ignore")
    psnr_average = parse_psnr_average(psnr_text)
    ssim_metrics = parse_ssim_all(ssim_text)
    if psnr_average is None or ssim_metrics is None:
        missing = "PSNR/SSIM" if psnr_average is None and ssim_metrics is None else (
            "PSNR" if psnr_average is None else "SSIM"
        )
        write_result(
            context,
            "FAIL",
            codec_label,
            "quality",
            case.case_name,
            case.size,
            str(case.rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"未能从日志提取 {missing} 指标，详见 logs/{psnr_log.name} 和 logs/{ssim_log.name}",
            str(artifact),
            backend="ffmpeg",
        )
        return

    ssim_all, ssim_db = ssim_metrics
    bytes_written = artifact.stat().st_size
    width, height = parse_size(case.size)
    total_elapsed = encode_result.elapsed + psnr_result.elapsed + ssim_result.elapsed
    total_user = encode_result.user + psnr_result.user + ssim_result.user
    total_sys = encode_result.sys + psnr_result.sys + ssim_result.sys
    fps = calc_fps(frames, total_elapsed)
    write_result(
        context,
        "PASS",
        codec_label,
        "quality",
        case.case_name,
        case.size,
        str(case.rate),
        str(frames),
        format_float(total_elapsed),
        fps,
        calc_realtime(fps, case.rate),
        calc_cpu_pct(total_user, total_sys, total_elapsed),
        (
            f"target_bpp={calc_target_bpp(case.bitrate, width, height, case.rate)} "
            f"avg_bpp={calc_avg_bpp(bytes_written, frames, width, height)} "
            f"psnr_avg={psnr_average} ssim_all={ssim_all} ssim_db={ssim_db}"
        ),
        str(artifact),
        backend="ffmpeg",
    )


def run_ffmpeg_generated_encode_to_file(
    context: BenchmarkContext,
    codec_label: str,
    case_name: str,
    size: str,
    rate: int,
    frames: int,
    artifact: Path,
    *,
    encoder_candidates: Sequence[str],
    muxer: str,
    bitrate: str,
    pixel_format: str,
    extra_options: Sequence[str] = (),
    unavailable_re: re.Pattern[str] | None = None,
) -> str | None:
    slug = codec_label.lower()
    log_path = context.paths.log_dir / f"{slug}_encode_{case_name}.log"
    encoder = first_available_ffmpeg_codec("encoders", encoder_candidates)
    if encoder is None:
        write_result(
            context,
            "UNAVAILABLE",
            codec_label,
            "encode",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"未发现可用的 ffmpeg {codec_label} 编码器",
            str(artifact),
            backend="ffmpeg",
        )
        return None

    source_args = build_source_input_args(context.config.source, size, rate)
    command = build_ffmpeg_generate_command(
        artifact,
        size,
        rate,
        frames,
        encoder=encoder,
        muxer=muxer,
        pixel_format=pixel_format,
        bitrate=bitrate,
        extra_options=extra_options,
        source_args=source_args,
    )
    result = timed_run(command, log_path, cwd=context.config.repo_root)
    log_text = log_path.read_text(encoding="utf-8", errors="ignore")
    if result.returncode != 0:
        unavailable = log_matches_unavailable(log_text, unavailable_re)
        write_result(
            context,
            "UNAVAILABLE" if unavailable else "FAIL",
            codec_label,
            "encode",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            (
                f"ffmpeg {encoder} 当前环境不可用，详见 logs/{log_path.name}"
                if unavailable
                else f"ffmpeg {encoder} 编码失败，详见 logs/{log_path.name}"
            ),
            str(artifact),
            backend="ffmpeg",
        )
        return None

    bytes_written = artifact.stat().st_size
    width, height = parse_size(size)
    fps = calc_fps(frames, result.elapsed)
    write_result(
        context,
        "PASS",
        codec_label,
        "encode",
        case_name,
        size,
        str(rate),
        str(frames),
        format_float(result.elapsed),
        fps,
        calc_realtime(fps, rate),
        calc_cpu_pct(result.user, result.sys, result.elapsed),
        f"encoder={encoder} avg_bpp={calc_avg_bpp(bytes_written, frames, width, height)}",
        str(artifact),
        backend="ffmpeg",
    )
    return encoder


def run_ffmpeg_roundtrip(
    context: BenchmarkContext,
    codec_label: str,
    ffmpeg_codec: str,
    muxer: str,
    case_name: str,
    size: str,
    rate: int,
    frames: int,
    bitrate: str,
    artifact: Path,
) -> None:
    slug = codec_label.lower()
    encode_log = context.paths.log_dir / f"{slug}_pipeline_encode_{case_name}.log"
    decode_log = context.paths.log_dir / f"{slug}_pipeline_decode_{case_name}.log"
    source_args = build_source_input_args(context.config.source, size, rate)
    encode_command = build_ffmpeg_generate_command(
        artifact,
        size,
        rate,
        frames,
        encoder=ffmpeg_codec,
        muxer=muxer,
        pixel_format="nv12",
        bitrate=bitrate,
        source_args=source_args,
    )
    encode_result = timed_run(encode_command, encode_log, cwd=context.config.repo_root)
    encode_text = encode_log.read_text(encoding="utf-8", errors="ignore")
    if encode_result.returncode != 0:
        unavailable = log_matches_unavailable(encode_text)
        write_result(
            context,
            "UNAVAILABLE" if unavailable else "FAIL",
            codec_label,
            "stream-pipeline",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            (
                f"ffmpeg {ffmpeg_codec} 当前环境不可用，详见 logs/{encode_log.name}"
                if unavailable
                else f"ffmpeg {ffmpeg_codec} roundtrip 编码失败，详见 logs/{encode_log.name}"
            ),
            str(artifact),
            backend="ffmpeg",
        )
        return

    decode_command = build_ffmpeg_hardware_decode_command(artifact, frames, ffmpeg_codec)
    decode_result = timed_run(decode_command, decode_log, cwd=context.config.repo_root)
    decode_text = decode_log.read_text(encoding="utf-8", errors="ignore")
    if decode_result.returncode != 0:
        unavailable = log_matches_unavailable(decode_text)
        write_result(
            context,
            "UNAVAILABLE" if unavailable else "FAIL",
            codec_label,
            "stream-pipeline",
            case_name,
            size,
            str(rate),
            str(frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            (
                f"ffmpeg {ffmpeg_codec} 当前环境不可用，详见 logs/{decode_log.name}"
                if unavailable
                else f"ffmpeg {ffmpeg_codec} roundtrip 解码失败，详见 logs/{decode_log.name}"
            ),
            str(artifact),
            backend="ffmpeg",
        )
        return

    bytes_written = artifact.stat().st_size
    width, height = parse_size(size)
    total_elapsed = encode_result.elapsed + decode_result.elapsed
    total_user = encode_result.user + decode_result.user
    total_sys = encode_result.sys + decode_result.sys
    fps = calc_fps(frames, total_elapsed)
    write_result(
        context,
        "PASS",
        codec_label,
        "stream-pipeline",
        case_name,
        size,
        str(rate),
        str(frames),
        format_float(total_elapsed),
        fps,
        calc_realtime(fps, rate),
        calc_cpu_pct(total_user, total_sys, total_elapsed),
        note=(
            f"encoder={ffmpeg_codec} decoder={ffmpeg_codec} "
            f"avg_bpp={calc_avg_bpp(bytes_written, frames, width, height)}"
        ),
        artifact=str(artifact),
        backend="ffmpeg",
    )
