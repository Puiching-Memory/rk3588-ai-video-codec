from __future__ import annotations

import os
import re
import resource
import shutil
import subprocess
import time
from collections.abc import Sequence
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import TextIO

REPO_ROOT = Path(__file__).resolve().parents[2]
SUMMARY_HEADER = (
    "status\tcodec\ttype\tcase\tsize\trate\tframes\telapsed_s\tfps\trealtime\tcpu\tnote\tartifact\n"
)
AV1_HW_FAILURE_RE = re.compile(
    r"CRITICAL|assertion|not support|Failed to get pixel format|No AV1 codec data found",
    re.IGNORECASE,
)


class BenchmarkError(RuntimeError):
    pass


@dataclass(frozen=True)
class ProfileSettings:
    hd_frames: int
    hd_loops: int
    uhd_frames: int
    uhd_loops: int
    pipeline_frames: int
    av1_width: int
    av1_height: int
    av1_frames: int


@dataclass
class TimedProcessResult:
    returncode: int
    elapsed: float
    user: float
    sys: float


@dataclass
class ResultRow:
    status: str
    codec: str
    type: str
    case_name: str
    size: str
    rate: str
    frames: str
    elapsed: str
    fps: str
    realtime: str
    cpu: str
    note: str
    artifact: str

    def to_tsv(self) -> str:
        return "\t".join(
            [
                self.status,
                self.codec,
                self.type,
                self.case_name,
                self.size,
                self.rate,
                self.frames,
                self.elapsed,
                self.fps,
                self.realtime,
                self.cpu,
                self.note,
                self.artifact,
            ]
        )


PROFILE_SETTINGS = {
    "quick": ProfileSettings(
        hd_frames=120,
        hd_loops=1,
        uhd_frames=60,
        uhd_loops=1,
        pipeline_frames=120,
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
        av1_width=1280,
        av1_height=720,
        av1_frames=60,
    ),
}


def sanitize_field(value: str = "") -> str:
    return value.replace("\t", " ").replace("\n", " ")


def format_float(value: float) -> str:
    return f"{value:.3f}"


def calc_fps(frames: int, elapsed: float) -> str:
    if elapsed <= 0:
        return "n/a"
    return f"{frames / elapsed:.1f}"


def calc_realtime(fps: str, rate: int) -> str:
    if fps == "n/a" or rate <= 0:
        return "n/a"
    return f"{float(fps) / rate:.2f}"


def calc_cpu_pct(user: float, sys_time: float, elapsed: float) -> str:
    if elapsed <= 0:
        return "n/a"
    return f"{((user + sys_time) / elapsed) * 100:.0f}%"


def capture_command(command: Sequence[str], cwd: Path | None = None) -> str:
    completed = subprocess.run(
        list(command),
        cwd=cwd,
        capture_output=True,
        text=True,
        check=False,
    )
    parts = [part for part in (completed.stdout, completed.stderr) if part]
    return "".join(parts).strip()


def command_exists(name: str) -> bool:
    return shutil.which(name) is not None


def ffmpeg_has_codec(kind: str, codec: str) -> bool:
    output = capture_command(["ffmpeg", "-hide_banner", f"-{kind}"])
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 2 and re.fullmatch(r"[A-Z.]+", parts[0]) and parts[1] == codec:
            return True
    return False


def gst_has(element: str) -> bool:
    completed = subprocess.run(
        ["gst-inspect-1.0", element],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return completed.returncode == 0


def timed_run(
    command: Sequence[str],
    log_path: Path,
    *,
    cwd: Path | None = None,
    stdout: TextIO | int | None = None,
) -> TimedProcessResult:
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.perf_counter()
    with log_path.open("w", encoding="utf-8") as stderr_file:
        completed = subprocess.run(
            list(command),
            cwd=cwd,
            stdout=subprocess.DEVNULL if stdout is None else stdout,
            stderr=stderr_file,
            text=True,
            check=False,
        )
    elapsed = time.perf_counter() - start
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    return TimedProcessResult(
        returncode=completed.returncode,
        elapsed=elapsed,
        user=after.ru_utime - before.ru_utime,
        sys=after.ru_stime - before.ru_stime,
    )


@dataclass
class BenchmarkRunner:
    profile: str = "full"
    out_dir: Path | None = None
    run_h264: bool = True
    run_h265: bool = True
    run_av1: bool = True
    run_4k: bool = True
    strict: bool = False
    repo_root: Path = REPO_ROOT
    rows: list[ResultRow] = field(default_factory=list)
    fail_count: int = 0
    unavailable_count: int = 0

    def __post_init__(self) -> None:
        self.settings = PROFILE_SETTINGS[self.profile]
        if self.out_dir is None:
            self.out_dir = self.repo_root / "results" / datetime.now().strftime(
                "%Y%m%d-%H%M%S"
            )
        self.log_dir = self.out_dir / "logs"
        self.artifact_dir = self.out_dir / "artifacts"
        self.summary_tsv = self.out_dir / "summary.tsv"
        self.summary_md = self.out_dir / "summary.md"
        self.system_txt = self.out_dir / "system.txt"
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.artifact_dir.mkdir(parents=True, exist_ok=True)
        self.summary_tsv.write_text(SUMMARY_HEADER, encoding="utf-8")

    def ensure_requirements(self) -> None:
        missing = [
            name
            for name in ("ffmpeg", "ffprobe", "gst-launch-1.0", "gst-inspect-1.0")
            if not command_exists(name)
        ]
        if missing:
            raise BenchmarkError(f"缺少命令: {', '.join(missing)}")

    def write_result(
        self,
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
    ) -> None:
        row = ResultRow(
            status=status,
            codec=codec,
            type=type_,
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
        self.rows.append(row)
        with self.summary_tsv.open("a", encoding="utf-8") as file:
            file.write(row.to_tsv() + "\n")

        if status == "FAIL":
            self.fail_count += 1
        elif status == "UNAVAILABLE":
            self.unavailable_count += 1

        suffix = f" {row.note}" if row.note else ""
        print(f"[{status}] {codec} {type_} {case_name}{suffix}")

    def write_system_info(self) -> None:
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

        lines = [
            f"timestamp={datetime.now().astimezone().isoformat()}",
            f"profile={self.profile}",
            f"out_dir={self.out_dir}",
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
            "\n".join(capture_command(["ffmpeg", "-hide_banner", "-version"]).splitlines()[:4]),
            "",
            "[ffmpeg-hwaccels]",
            capture_command(["ffmpeg", "-hide_banner", "-hwaccels"]),
            "",
            "[v4l2-devices]",
        ]
        if command_exists("v4l2-ctl"):
            lines.append(capture_command(["v4l2-ctl", "--list-devices"]))
        else:
            lines.append("v4l2-ctl not found")

        self.system_txt.write_text("\n".join(lines) + "\n", encoding="utf-8")

    def run_ffmpeg_encode(
        self,
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
        log_path = self.log_dir / f"{slug}_encode_{case_name}.log"
        command = [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "error",
            "-f",
            "lavfi",
            "-i",
            f"testsrc2=size={size}:rate={rate}",
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
        result = timed_run(command, log_path, cwd=self.repo_root)
        if result.returncode != 0:
            self.write_result(
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
            )
            return

        bytes_written = artifact.stat().st_size
        fps = calc_fps(frames, result.elapsed)
        self.write_result(
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
            note=f"avg_mbps={(bytes_written * 8 * rate) / (frames * 1000000):.2f}",
            artifact=str(artifact),
        )

    def run_ffmpeg_decode(
        self,
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
        log_path = self.log_dir / f"{slug}_decode_{case_name}.log"
        command = [
            "ffmpeg",
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
        result = timed_run(command, log_path, cwd=self.repo_root)
        if result.returncode != 0:
            self.write_result(
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
            )
            return

        fps = calc_fps(total_frames, result.elapsed)
        self.write_result(
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
        )

    def run_gstreamer_pipeline(
        self,
        codec_label: str,
        encoder: str,
        parser: str,
        case_name: str,
        width: int,
        height: int,
        rate: int,
        frames: int,
    ) -> None:
        slug = codec_label.lower()
        log_path = self.log_dir / f"{slug}_pipeline_{case_name}.log"
        if not (gst_has(encoder) and gst_has(parser) and gst_has("mppvideodec")):
            self.write_result(
                "UNAVAILABLE",
                codec_label,
                "stream-pipeline",
                case_name,
                f"{width}x{height}",
                str(rate),
                str(frames),
                "n/a",
                "n/a",
                "n/a",
                "n/a",
                f"缺少 GStreamer Rockchip {codec_label} 元件",
            )
            return

        command = [
            "gst-launch-1.0",
            "-q",
            "videotestsrc",
            f"num-buffers={frames}",
            "pattern=smpte",
            "!",
            f"video/x-raw,format=NV12,width={width},height={height},framerate={rate}/1",
            "!",
            encoder,
            "!",
            parser,
            "!",
            "queue",
            "!",
            "mppvideodec",
            "!",
            "fakesink",
            "sync=false",
        ]
        result = timed_run(command, log_path, cwd=self.repo_root)
        if result.returncode != 0:
            self.write_result(
                "FAIL",
                codec_label,
                "stream-pipeline",
                case_name,
                f"{width}x{height}",
                str(rate),
                str(frames),
                "n/a",
                "n/a",
                "n/a",
                "n/a",
                f"GStreamer {codec_label} 编解码流水线失败，详见 logs/{log_path.name}",
            )
            return

        fps = calc_fps(frames, result.elapsed)
        self.write_result(
            "PASS",
            codec_label,
            "stream-pipeline",
            case_name,
            f"{width}x{height}",
            str(rate),
            str(frames),
            format_float(result.elapsed),
            fps,
            calc_realtime(fps, rate),
            calc_cpu_pct(result.user, result.sys, result.elapsed),
            note=f"{encoder} -> mppvideodec",
        )

    def run_h26x_suite(
        self,
        codec_label: str,
        ffmpeg_codec: str,
        encoder: str,
        parser: str,
        muxer: str,
        extension: str,
    ) -> None:
        if not (
            ffmpeg_has_codec("encoders", ffmpeg_codec)
            and ffmpeg_has_codec("decoders", ffmpeg_codec)
        ):
            self.write_result(
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
            )
            return

        hd_artifact = self.artifact_dir / f"{codec_label.lower()}_1080p60.{extension}"
        self.run_ffmpeg_encode(
            codec_label,
            ffmpeg_codec,
            muxer,
            "1080p60",
            "1920x1080",
            60,
            self.settings.hd_frames,
            "8M",
            hd_artifact,
        )
        self.run_ffmpeg_decode(
            codec_label,
            ffmpeg_codec,
            "1080p60",
            "1920x1080",
            60,
            self.settings.hd_frames,
            self.settings.hd_loops,
            hd_artifact,
        )
        self.run_gstreamer_pipeline(
            codec_label,
            encoder,
            parser,
            "1080p60",
            1920,
            1080,
            60,
            self.settings.pipeline_frames,
        )

        if self.run_4k:
            uhd_artifact = self.artifact_dir / f"{codec_label.lower()}_2160p60.{extension}"
            self.run_ffmpeg_encode(
                codec_label,
                ffmpeg_codec,
                muxer,
                "2160p60",
                "3840x2160",
                60,
                self.settings.uhd_frames,
                "25M",
                uhd_artifact,
            )
            self.run_ffmpeg_decode(
                codec_label,
                ffmpeg_codec,
                "2160p60",
                "3840x2160",
                60,
                self.settings.uhd_frames,
                self.settings.uhd_loops,
                uhd_artifact,
            )

    def run_av1_tests(self) -> None:
        case_name = "720p30"
        size = f"{self.settings.av1_width}x{self.settings.av1_height}"
        rate = 30
        sample = self.artifact_dir / f"av1_probe_{self.profile}.webm"
        gen_log = self.log_dir / f"av1_generate_{self.profile}.log"
        sw_log = self.log_dir / f"av1_software_decode_{self.profile}.log"
        hw_log = self.log_dir / f"av1_hardware_decode_{self.profile}.log"

        if not ffmpeg_has_codec("encoders", "av1_rkmpp") and not gst_has("mppav1enc"):
            self.write_result(
                "UNAVAILABLE",
                "AV1",
                "encode",
                case_name,
                size,
                str(rate),
                str(self.settings.av1_frames),
                "n/a",
                "n/a",
                "n/a",
                "n/a",
                "未发现可用的 AV1 硬编码器",
            )

        required = ("av1enc", "av1parse", "av1dec", "webmmux", "matroskademux", "mppvideodec")
        if not all(gst_has(element) for element in required):
            self.write_result(
                "UNAVAILABLE",
                "AV1",
                "decode",
                case_name,
                size,
                str(rate),
                str(self.settings.av1_frames),
                "n/a",
                "n/a",
                "n/a",
                "n/a",
                "缺少 AV1 探测所需的 GStreamer 元件",
            )
            return

        generate_command = [
            "gst-launch-1.0",
            "-q",
            "videotestsrc",
            f"num-buffers={self.settings.av1_frames}",
            "pattern=smpte",
            "!",
            f"video/x-raw,format=I420,width={self.settings.av1_width},height={self.settings.av1_height},framerate={rate}/1",
            "!",
            "av1enc",
            "cpu-used=5",
            "threads=8",
            "row-mt=true",
            "end-usage=q",
            "!",
            "webmmux",
            "!",
            "filesink",
            f"location={sample}",
        ]
        result = timed_run(generate_command, gen_log, cwd=self.repo_root)
        if result.returncode != 0:
            self.write_result(
                "FAIL",
                "AV1",
                "decode",
                case_name,
                size,
                str(rate),
                str(self.settings.av1_frames),
                "n/a",
                "n/a",
                "n/a",
                "n/a",
                f"AV1 样本生成失败，详见 logs/{gen_log.name}",
                str(sample),
            )
            return

        sw_command = [
            "gst-launch-1.0",
            "-q",
            "filesrc",
            f"location={sample}",
            "!",
            "matroskademux",
            "!",
            "av1parse",
            "!",
            "av1dec",
            "!",
            "fakesink",
            "sync=false",
        ]
        sw_result = timed_run(sw_command, sw_log, cwd=self.repo_root)
        if sw_result.returncode != 0:
            self.write_result(
                "FAIL",
                "AV1",
                "decode",
                case_name,
                size,
                str(rate),
                str(self.settings.av1_frames),
                "n/a",
                "n/a",
                "n/a",
                "n/a",
                "AV1 软件解码基线失败，样本不可用",
                str(sample),
            )
            return

        hw_command = [
            "gst-launch-1.0",
            "-q",
            "filesrc",
            f"location={sample}",
            "!",
            "matroskademux",
            "!",
            "av1parse",
            "!",
            "mppvideodec",
            "!",
            "fakesink",
            "sync=false",
        ]
        hw_result = timed_run(hw_command, hw_log, cwd=self.repo_root)
        if hw_result.returncode != 0:
            self.write_result(
                "FAIL",
                "AV1",
                "decode",
                case_name,
                size,
                str(rate),
                str(self.settings.av1_frames),
                "n/a",
                "n/a",
                "n/a",
                "n/a",
                f"mppvideodec 返回非 0，详见 logs/{hw_log.name}",
                str(sample),
            )
            return

        hw_log_text = hw_log.read_text(encoding="utf-8", errors="ignore")
        if AV1_HW_FAILURE_RE.search(hw_log_text):
            self.write_result(
                "UNAVAILABLE",
                "AV1",
                "decode",
                case_name,
                size,
                str(rate),
                str(self.settings.av1_frames),
                "n/a",
                "n/a",
                "n/a",
                "n/a",
                "软件解码基线通过，但硬解码协商或运行失败",
                str(sample),
            )
            return

        fps = calc_fps(self.settings.av1_frames, hw_result.elapsed)
        self.write_result(
            "PASS",
            "AV1",
            "decode",
            case_name,
            size,
            str(rate),
            str(self.settings.av1_frames),
            format_float(hw_result.elapsed),
            fps,
            calc_realtime(fps, rate),
            calc_cpu_pct(hw_result.user, hw_result.sys, hw_result.elapsed),
            "软件解码基线通过，mppvideodec 可用",
            str(sample),
        )

    def render_summary_md(self) -> None:
        lines = [
            "# RK3588 VPU Benchmark Summary",
            "",
            f"- Generated: {datetime.now().astimezone().isoformat()}",
            f"- Profile: {self.profile}",
            f"- Output: {self.out_dir}",
            "",
            "| Status | Codec | Type | Case | FPS | Real-time | CPU | Note |",
            "| --- | --- | --- | --- | ---: | ---: | ---: | --- |",
        ]
        for row in self.rows:
            lines.append(
                "| "
                f"{row.status} | {row.codec} | {row.type} | {row.case_name} | "
                f"{row.fps} | {row.realtime} | {row.cpu} | {row.note} |"
            )
        lines.extend(["", "See system.txt, logs/, artifacts/, and summary.tsv for raw data."])
        self.summary_md.write_text("\n".join(lines) + "\n", encoding="utf-8")

    def run(self) -> int:
        self.ensure_requirements()
        self.write_system_info()

        if self.run_h265:
            self.run_h26x_suite("H265", "hevc_rkmpp", "mpph265enc", "h265parse", "hevc", "hevc")

        if self.run_h264:
            self.run_h26x_suite("H264", "h264_rkmpp", "mpph264enc", "h264parse", "h264", "h264")

        if self.run_av1:
            self.run_av1_tests()

        self.render_summary_md()

        print()
        print(f"结果目录: {self.out_dir}")
        print(f"汇总文件: {self.summary_tsv}")
        print(f"说明文件: {self.summary_md}")

        if self.strict and (self.fail_count > 0 or self.unavailable_count > 0):
            return 1
        return 0
