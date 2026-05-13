from __future__ import annotations

import re
from collections.abc import Sequence
from pathlib import Path

from .process import capture_command

PREFERRED_FFMPEG_BIN = Path("/usr/local/ffmpeg-rockchip/bin/ffmpeg")
PREFERRED_FFPROBE_BIN = Path("/usr/local/ffmpeg-rockchip/bin/ffprobe")
FFMPEG_BIN = str(PREFERRED_FFMPEG_BIN) if PREFERRED_FFMPEG_BIN.exists() else "ffmpeg"
FFPROBE_BIN = str(PREFERRED_FFPROBE_BIN) if PREFERRED_FFPROBE_BIN.exists() else "ffprobe"
FFMPEG_HW_UNAVAILABLE_RE = re.compile(
    r"Could not find a valid device|"
    r"No such device|"
    r"Your platform doesn't support hardware accelerated|"
    r"Failed to get pixel format|No AV1 codec data found",
    re.IGNORECASE,
)


def ffmpeg_has_codec(kind: str, codec: str) -> bool:
    output = capture_command([FFMPEG_BIN, "-hide_banner", f"-{kind}"])
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 2 and re.fullmatch(r"[A-Z.]+", parts[0]) and parts[1] == codec:
            return True
    return False


def first_available_ffmpeg_codec(kind: str, codecs: Sequence[str]) -> str | None:
    for codec in codecs:
        if ffmpeg_has_codec(kind, codec):
            return codec
    return None


def log_matches_unavailable(log_text: str, extra_re: re.Pattern[str] | None = None) -> bool:
    return FFMPEG_HW_UNAVAILABLE_RE.search(log_text) is not None or (
        extra_re is not None and extra_re.search(log_text) is not None
    )


def build_ffmpeg_generate_command(
    sample: Path,
    size: str,
    rate: int,
    frames: int,
    *,
    encoder: str,
    muxer: str,
    pixel_format: str,
    bitrate: str | None = None,
    extra_options: Sequence[str] = (),
    source: str | None = None,
    source_args: Sequence[str] | None = None,
) -> list[str]:
    command = [
        FFMPEG_BIN,
        "-hide_banner",
        "-loglevel",
        "error",
    ]
    if source_args is not None:
        command.extend(source_args)
    elif source is not None:
        command.extend(["-i", source])
    else:
        command.extend([
            "-f", "lavfi",
            "-i", f"testsrc2=size={size}:rate={rate}",
        ])
    command.extend([
        "-frames:v",
        str(frames),
        "-pix_fmt",
        pixel_format,
        "-c:v",
        encoder,
    ])
    if bitrate is not None:
        command.extend(["-b:v", bitrate])
    command.extend(extra_options)
    command.extend(["-f", muxer, "-y", str(sample)])
    return command


def build_ffmpeg_software_decode_command(sample: Path, frames: int) -> list[str]:
    return [
        FFMPEG_BIN,
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(sample),
        "-frames:v",
        str(frames),
        "-f",
        "null",
        "-",
    ]


def build_ffmpeg_hardware_decode_command(sample: Path, frames: int, decoder: str) -> list[str]:
    return [
        FFMPEG_BIN,
        "-hide_banner",
        "-loglevel",
        "error",
        "-c:v",
        decoder,
        "-i",
        str(sample),
        "-frames:v",
        str(frames),
        "-f",
        "null",
        "-",
    ]
