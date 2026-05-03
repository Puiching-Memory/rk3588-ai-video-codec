from __future__ import annotations

from ..domain import build_quality_cases
from ..ffmpeg_backend import ffmpeg_has_codec
from ..operations import (
    run_ffmpeg_decode,
    run_ffmpeg_encode,
    run_ffmpeg_quality,
    run_ffmpeg_roundtrip,
)
from ..reporting import write_result
from ..runtime import BenchmarkContext


def run_h26x_suite(
    context: BenchmarkContext,
    codec_label: str,
    ffmpeg_codec: str,
    muxer: str,
    extension: str,
) -> None:
    if not (
        ffmpeg_has_codec("encoders", ffmpeg_codec)
        and ffmpeg_has_codec("decoders", ffmpeg_codec)
    ):
        write_result(
            context,
            "UNAVAILABLE",
            codec_label,
            "suite",
            "all",
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"未发现 {ffmpeg_codec} 编解码器",
            backend="ffmpeg",
        )
        return

    settings = context.settings
    artifact_dir = context.paths.artifact_dir
    hd_artifact = artifact_dir / f"{codec_label.lower()}_1080p60.{extension}"
    run_ffmpeg_encode(
        context,
        codec_label,
        ffmpeg_codec,
        muxer,
        "1080p60",
        "1920x1080",
        60,
        settings.hd_frames,
        "8M",
        hd_artifact,
    )
    run_ffmpeg_decode(
        context,
        codec_label,
        ffmpeg_codec,
        "1080p60",
        "1920x1080",
        60,
        settings.hd_frames,
        settings.hd_loops,
        hd_artifact,
    )
    run_ffmpeg_roundtrip(
        context,
        codec_label,
        ffmpeg_codec,
        muxer,
        "1080p60",
        "1920x1080",
        60,
        settings.pipeline_frames,
        "8M",
        artifact_dir / f"{codec_label.lower()}_pipeline_1080p60.{extension}",
    )

    if context.config.run_4k:
        uhd_artifact = artifact_dir / f"{codec_label.lower()}_2160p60.{extension}"
        run_ffmpeg_encode(
            context,
            codec_label,
            ffmpeg_codec,
            muxer,
            "2160p60",
            "3840x2160",
            60,
            settings.uhd_frames,
            "25M",
            uhd_artifact,
        )
        run_ffmpeg_decode(
            context,
            codec_label,
            ffmpeg_codec,
            "2160p60",
            "3840x2160",
            60,
            settings.uhd_frames,
            settings.uhd_loops,
            uhd_artifact,
        )


def run_h26x_quality_suite(
    context: BenchmarkContext,
    codec_label: str,
    ffmpeg_codec: str,
    muxer: str,
    extension: str,
) -> None:
    if not ffmpeg_has_codec("encoders", ffmpeg_codec):
        write_result(
            context,
            "UNAVAILABLE",
            codec_label,
            "quality",
            "ladder",
            "n/a",
            "n/a",
            str(context.settings.quality_frames),
            "n/a",
            "n/a",
            "n/a",
            "n/a",
            f"未发现 {ffmpeg_codec} 硬编码器，无法执行画质档位测试",
            backend="ffmpeg",
        )
        return

    for case in build_quality_cases():
        artifact = (
            context.paths.artifact_dir
            / f"{codec_label.lower()}_quality_{case.case_name}.{extension}"
        )
        run_ffmpeg_quality(
            context,
            codec_label,
            ffmpeg_codec,
            muxer,
            case,
            context.settings.quality_frames,
            artifact,
        )
