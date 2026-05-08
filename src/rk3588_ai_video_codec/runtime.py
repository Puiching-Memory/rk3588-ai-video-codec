from __future__ import annotations

from dataclasses import dataclass, field, replace
from datetime import datetime
from pathlib import Path

from .domain import PROFILE_SETTINGS, ProfileSettings
from .process import ResultRow

REPO_ROOT = Path(__file__).resolve().parents[2]
SUMMARY_HEADER = (
    "status\tcodec\ttype\tbackend\tcase\tsize\trate\tframes\telapsed_s\tfps\trealtime\tcpu\tnote\tartifact\n"
)


@dataclass(frozen=True)
class BenchmarkConfig:
    profile: str = "full"
    out_dir: Path | None = None
    run_h264: bool = True
    run_h265: bool = True
    run_av1: bool = True
    run_4k: bool = True
    run_throughput: bool = True
    run_quality: bool = False
    run_extra_codecs: bool = True
    run_extended_quality: bool = False
    repo_root: Path = REPO_ROOT


@dataclass(frozen=True)
class BenchmarkPaths:
    out_dir: Path
    log_dir: Path
    artifact_dir: Path
    summary_tsv: Path
    summary_md: Path
    system_txt: Path


@dataclass
class BenchmarkState:
    rows: list[ResultRow] = field(default_factory=list)
    fail_count: int = 0
    unavailable_count: int = 0


@dataclass(frozen=True)
class BenchmarkContext:
    config: BenchmarkConfig
    settings: ProfileSettings
    paths: BenchmarkPaths
    state: BenchmarkState


def prepare_context(config: BenchmarkConfig) -> BenchmarkContext:
    settings = PROFILE_SETTINGS[config.profile]
    out_dir = config.out_dir
    if out_dir is None:
        out_dir = config.repo_root / "results" / datetime.now().strftime("%Y%m%d-%H%M%S")

    resolved_config = replace(config, out_dir=out_dir)
    paths = BenchmarkPaths(
        out_dir=out_dir,
        log_dir=out_dir / "logs",
        artifact_dir=out_dir / "artifacts",
        summary_tsv=out_dir / "summary.tsv",
        summary_md=out_dir / "summary.md",
        system_txt=out_dir / "system.txt",
    )
    paths.out_dir.mkdir(parents=True, exist_ok=True)
    paths.log_dir.mkdir(parents=True, exist_ok=True)
    paths.artifact_dir.mkdir(parents=True, exist_ok=True)
    paths.summary_tsv.write_text(SUMMARY_HEADER, encoding="utf-8")
    return BenchmarkContext(
        config=resolved_config,
        settings=settings,
        paths=paths,
        state=BenchmarkState(),
    )