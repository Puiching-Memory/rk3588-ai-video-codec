from __future__ import annotations

import re
from collections.abc import Sequence
from pathlib import Path

from ..domain import (
    QualityCase,
    build_extra_quality_cases,
    build_quality_compare_filter,
    calc_avg_bpp,
    calc_target_bpp,
    parse_psnr_average,
    parse_size,
    parse_ssim_all,
)
from ..ffmpeg_backend import (
    FFMPEG_BIN,
    FFMPEG_HW_UNAVAILABLE_RE,
    build_ffmpeg_generate_command,
    build_ffmpeg_software_decode_command,
    ffmpeg_has_codec,
    first_available_ffmpeg_codec,
    log_matches_unavailable,
)
from ..process import calc_cpu_pct, calc_fps, calc_realtime, format_float, timed_run
from ..reporting import write_result
from ..runtime import BenchmarkContext
from ..test_sequences import build_source_input_args


def run_generated_quality_probe(
    context: BenchmarkContext,
    codec_label: str,
    ffmpeg_decoder: str,
    case: QualityCase,
    frames: int,
    artifact: Path,
    *,
    encoder_candidates: Sequence[str],
    muxer: str,
    pixel_format: str,
    extra_options: Sequence[str] = (),
    hardware_failure_re: re.Pattern[str] | None = None,
) -> None:
    if not ffmpeg_has_codec("decoders", ffmpeg_decoder):
        write_result(
            context,
            "UNAVAILABLE",
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
            f"未发现 {ffmpeg_decoder} 硬解码器，无法执行画质测试",
            backend="ffmpeg",
        )
        return

    encoder = first_available_ffmpeg_codec("encoders", encoder_candidates)
    if encoder is None:
        write_result(
            context,
            "UNAVAILABLE",
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
            f"未发现可用于生成 {codec_label} 画质样本的 ffmpeg 编码器",
            str(artifact),
            backend="ffmpeg",
        )
        return

    slug = codec_label.lower()
    encode_log = context.paths.log_dir / f"{slug}_quality_encode_{case.case_name}.log"
    software_log = context.paths.log_dir / f"{slug}_quality_software_decode_{case.case_name}.log"
    psnr_log = context.paths.log_dir / f"{slug}_quality_psnr_{case.case_name}.log"
    ssim_log = context.paths.log_dir / f"{slug}_quality_ssim_{case.case_name}.log"

    source_args = build_source_input_args(context.config.source, case.size, case.rate)
    generate_command = build_ffmpeg_generate_command(
        artifact,
        case.size,
        case.rate,
        frames,
        encoder=encoder,
        muxer=muxer,
        pixel_format=pixel_format,
        bitrate=case.bitrate,
        extra_options=extra_options,
        source_args=source_args,
    )
    encode_result = timed_run(generate_command, encode_log, cwd=context.config.repo_root)
    encode_text = encode_log.read_text(encoding="utf-8", errors="ignore")
    if log_matches_unavailable(encode_text, hardware_failure_re):
        write_result(
            context,
            "UNAVAILABLE",
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
            f"ffmpeg {encoder} 当前环境不可用，详见 logs/{encode_log.name}",
            str(artifact),
            backend="ffmpeg",
        )
        return
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
            f"{codec_label} 画质样本生成失败，详见 logs/{encode_log.name}",
            str(artifact),
            backend="ffmpeg",
        )
        return

    software_command = build_ffmpeg_software_decode_command(artifact, frames)
    software_result = timed_run(software_command, software_log, cwd=context.config.repo_root)
    if software_result.returncode != 0:
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
            f"{codec_label} 软件解码基线失败，详见 logs/{software_log.name}",
            str(artifact),
            backend="ffmpeg",
        )
        return

    compare_base = [
        FFMPEG_BIN,
        "-hide_banner",
        "-loglevel",
        "info",
        *source_args,
        "-c:v",
        ffmpeg_decoder,
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
    psnr_text = psnr_log.read_text(encoding="utf-8", errors="ignore")
    if log_matches_unavailable(psnr_text, hardware_failure_re):
        write_result(
            context,
            "UNAVAILABLE",
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
            f"{ffmpeg_decoder} 当前环境不可用，详见 logs/{psnr_log.name}",
            str(artifact),
            backend="ffmpeg",
        )
        return
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
    ssim_text = ssim_log.read_text(encoding="utf-8", errors="ignore")
    if log_matches_unavailable(ssim_text, hardware_failure_re):
        write_result(
            context,
            "UNAVAILABLE",
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
            f"{ffmpeg_decoder} 当前环境不可用，详见 logs/{ssim_log.name}",
            str(artifact),
            backend="ffmpeg",
        )
        return
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

    psnr_average = parse_psnr_average(psnr_text)
    ssim_metrics = parse_ssim_all(ssim_text)
    if psnr_average is None or ssim_metrics is None:
        missing_metric = "PSNR/SSIM" if psnr_average is None and ssim_metrics is None else (
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
            (
                f"未能从日志提取 {missing_metric} 指标，"
                f"详见 logs/{psnr_log.name} 和 logs/{ssim_log.name}"
            ),
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
            f"psnr_avg={psnr_average} ssim_all={ssim_all} ssim_db={ssim_db} "
            f"encoder={encoder} decoder={ffmpeg_decoder}"
        ),
        str(artifact),
        backend="ffmpeg",
    )
def run_vp8_quality_suite(context: BenchmarkContext) -> None:
    frames = min(context.settings.quality_frames, 60)
    for case in build_extra_quality_cases("VP8"):
        artifact = context.paths.artifact_dir / f"vp8_quality_{case.case_name}.webm"
        run_generated_quality_probe(
            context,
            "VP8",
            "vp8_rkmpp",
            case,
            frames,
            artifact,
            encoder_candidates=("vp8_rkmpp", "vp8_v4l2m2m"),
            muxer="webm",
            pixel_format="nv12",
        )


def run_vp9_quality_suite(context: BenchmarkContext) -> None:
    frames = min(context.settings.quality_frames, 60)
    for case in build_extra_quality_cases("VP9"):
        artifact = context.paths.artifact_dir / f"vp9_quality_{case.case_name}.webm"
        run_generated_quality_probe(
            context,
            "VP9",
            "vp9_rkmpp",
            case,
            frames,
            artifact,
            encoder_candidates=("vp9_rkmpp", "libvpx-vp9", "vp9_v4l2m2m"),
            muxer="webm",
            pixel_format="yuv420p",
        )


def run_av1_quality_suite(context: BenchmarkContext) -> None:
    frames = min(context.settings.quality_frames, 60)
    for case in build_extra_quality_cases("AV1"):
        artifact = context.paths.artifact_dir / f"av1_quality_{case.case_name}.webm"
        run_generated_quality_probe(
            context,
            "AV1",
            "av1_rkmpp",
            case,
            frames,
            artifact,
            encoder_candidates=("av1_rkmpp", "libsvtav1", "librav1e", "libaom-av1"),
            muxer="webm",
            pixel_format="yuv420p",
            hardware_failure_re=FFMPEG_HW_UNAVAILABLE_RE,
        )


def run_extended_quality_suites(context: BenchmarkContext) -> None:
    run_vp8_quality_suite(context)
    run_vp9_quality_suite(context)
    run_av1_quality_suite(context)
