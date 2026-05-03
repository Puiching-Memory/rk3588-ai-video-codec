from __future__ import annotations

import re
from dataclasses import dataclass

PSNR_AVERAGE_RE = re.compile(r"average:(inf|[0-9]+(?:\.[0-9]+)?)")
SSIM_ALL_RE = re.compile(r"All:(inf|[0-9]+(?:\.[0-9]+)?)\s+\((inf|[0-9]+(?:\.[0-9]+)?)")


@dataclass(frozen=True)
class ProfileSettings:
    hd_frames: int
    hd_loops: int
    uhd_frames: int
    uhd_loops: int
    pipeline_frames: int
    quality_frames: int
    av1_width: int
    av1_height: int
    av1_frames: int


@dataclass(frozen=True)
class QualityCase:
    case_name: str
    size: str
    rate: int
    bitrate: str


PROFILE_SETTINGS = {
    "quick": ProfileSettings(
        hd_frames=120,
        hd_loops=1,
        uhd_frames=60,
        uhd_loops=1,
        pipeline_frames=120,
        quality_frames=120,
        av1_width=640,
        av1_height=360,
        av1_frames=30,
    ),
    "full": ProfileSettings(
        hd_frames=600,
        hd_loops=4,
        uhd_frames=300,
        uhd_loops=3,
        pipeline_frames=600,
        quality_frames=300,
        av1_width=1280,
        av1_height=720,
        av1_frames=60,
    ),
}


def parse_psnr_average(log_text: str) -> str | None:
    match = PSNR_AVERAGE_RE.search(log_text)
    if not match:
        return None
    return match.group(1)


def parse_ssim_all(log_text: str) -> tuple[str, str] | None:
    match = SSIM_ALL_RE.search(log_text)
    if not match:
        return None
    return match.group(1), match.group(2)


def build_quality_cases() -> tuple[QualityCase, ...]:
    return (
        QualityCase("360p30_500kbps", "640x360", 30, "500k"),
        QualityCase("480p30_1000kbps", "848x480", 30, "1000k"),
        QualityCase("720p30_1500kbps", "1280x720", 30, "1500k"),
        QualityCase("1080p30_2500kbps", "1920x1080", 30, "2500k"),
        QualityCase("1080p30_3500kbps", "1920x1080", 30, "3500k"),
    )


def build_extra_quality_cases(codec_label: str) -> tuple[QualityCase, ...]:
    cases = build_quality_cases()
    if codec_label == "AV1":
        return (cases[0], cases[2])
    return cases[:3]


def calc_avg_kbps(bytes_written: int, frames: int, rate: int) -> str:
    if frames <= 0 or rate <= 0:
        return "n/a"
    avg_kbps = (bytes_written * 8 * rate) / (frames * 1000)
    return f"{avg_kbps:.1f}"


def bitrate_to_kbps(bitrate: str) -> int:
    lower = bitrate.lower()
    if lower.endswith("kbps"):
        return int(float(lower[:-4]))
    if lower.endswith("k"):
        return int(float(lower[:-1]))
    if lower.endswith("m"):
        return int(float(lower[:-1]) * 1000)
    return int(float(lower))


def bitrate_to_bps(bitrate: str) -> int:
    return bitrate_to_kbps(bitrate) * 1000


def build_quality_compare_filter(size: str, frames: int, metric: str) -> str:
    crop_size = size.replace("x", ":")
    return (
        f"[0:v]trim=end_frame={frames},setpts=PTS-STARTPTS,settb=AVTB,format=yuv420p,"
        f"crop={crop_size},setsar=1[ref];"
        f"[1:v]trim=end_frame={frames},setpts=PTS-STARTPTS,settb=AVTB,format=yuv420p,"
        f"crop={crop_size},setsar=1[dist];"
        f"[dist][ref]{metric}"
    )


def format_case_name(size: str, rate: int) -> str:
    try:
        _, height_text = size.split("x", maxsplit=1)
    except ValueError:
        return f"{size}_{rate}fps"

    if height_text in {"360", "480", "720", "1080", "2160"}:
        return f"{height_text}p{rate}"
    return f"{size}_{rate}fps"
