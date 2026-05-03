from __future__ import annotations

import argparse
from collections.abc import Sequence
from pathlib import Path

from .benchmark import BenchmarkError, BenchmarkRunner


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="benchmark-vpu",
        description="在 RK3588 上重复执行 H.264/H.265/AV1 VPU 基准测试。",
    )
    parser.add_argument(
        "--profile",
        choices=("quick", "full"),
        default="full",
        help="选择测试档位，默认 full",
    )
    parser.add_argument("--out-dir", type=Path, help="指定输出目录，默认 results/<timestamp>")
    mode_group = parser.add_mutually_exclusive_group()
    mode_group.add_argument("--h264-only", action="store_true", help="仅运行 H.264 测试")
    mode_group.add_argument("--h265-only", action="store_true", help="仅运行 H.265 测试")
    mode_group.add_argument("--av1-only", action="store_true", help="仅运行 AV1 测试")
    parser.add_argument("--skip-4k", action="store_true", help="跳过 4K H.264/H.265 测试")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="若存在 FAIL 或 UNAVAILABLE，返回非 0 退出码",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    run_h264 = True
    run_h265 = True
    run_av1 = True
    if args.h264_only:
        run_h264, run_h265, run_av1 = True, False, False
    elif args.h265_only:
        run_h264, run_h265, run_av1 = False, True, False
    elif args.av1_only:
        run_h264, run_h265, run_av1 = False, False, True

    runner = BenchmarkRunner(
        profile=args.profile,
        out_dir=args.out_dir,
        run_h264=run_h264,
        run_h265=run_h265,
        run_av1=run_av1,
        run_4k=not args.skip_4k,
        strict=args.strict,
    )
    try:
        return runner.run()
    except BenchmarkError as error:
        parser.exit(1, f"ERROR: {error}\n")
