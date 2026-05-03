from __future__ import annotations

import resource
import shutil
import subprocess
import time
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import TextIO


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
    backend: str
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
                self.backend,
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
