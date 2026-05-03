from __future__ import annotations

import argparse
from collections.abc import Sequence
from pathlib import Path

from .benchmark import BenchmarkConfig, BenchmarkError, BenchmarkRunner
from .plotting import render_summary_plots


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
        "--quality-ladder",
        action="store_true",
        help="追加 360p/480p/720p/1080p 目标码率的 PSNR/SSIM 画质还原度测试",
    )
    parser.add_argument(
        "--quality-only",
        action="store_true",
        help="仅运行画质还原度测试，不执行吞吐和 AV1 探测",
    )
    parser.add_argument(
        "--quality-extra-codecs",
        action="store_true",
        help="追加 MJPEG/VP8/VP9/AV1 的扩展画质测试与统一图表输入",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="若存在 FAIL 或 UNAVAILABLE，返回非 0 退出码",
    )
    parser.add_argument(
        "--plot-summary",
        type=Path,
        help="读取已有 summary.tsv 或结果目录并生成图表后退出",
    )
    parser.add_argument(
        "--plot-out-dir",
        type=Path,
        help="指定图表输出目录，默认 <summary_dir>/plots",
    )
    parser.add_argument(
        "--plot-title",
        help="自定义图表标题",
    )
    parser.add_argument(
        "--plot-charts",
        action="store_true",
        help="基准测试结束后自动生成图表",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.plot_summary is not None:
        if args.plot_charts:
            parser.error("--plot-summary 与 --plot-charts 不能同时使用")
        try:
            output_paths = render_summary_plots(
                args.plot_summary,
                out_dir=args.plot_out_dir,
                title=args.plot_title,
            )
        except BenchmarkError as error:
            parser.exit(1, f"ERROR: {error}\n")

        for output_path in output_paths:
            print(f"图表: {output_path}")
        return 0

    run_h264 = True
    run_h265 = True
    run_av1 = True
    if args.h264_only:
        run_h264, run_h265, run_av1 = True, False, False
    elif args.h265_only:
        run_h264, run_h265, run_av1 = False, True, False
    elif args.av1_only:
        run_h264, run_h265, run_av1 = False, False, True

    run_quality = args.quality_ladder or args.quality_only
    run_throughput = not args.quality_only
    run_extra_codecs = run_throughput and not (args.h264_only or args.h265_only or args.av1_only)
    run_extended_quality = args.quality_extra_codecs
    if run_quality and not (run_h264 or run_h265):
        parser.error("画质还原度测试仅支持 H.264/H.265，请勿与 --av1-only 组合")
    if run_extended_quality and (args.h264_only or args.h265_only or args.av1_only):
        parser.error("--quality-extra-codecs 不能与 --h264-only/--h265-only/--av1-only 组合")

    runner = BenchmarkRunner(
        BenchmarkConfig(
            profile=args.profile,
            out_dir=args.out_dir,
            run_h264=run_h264,
            run_h265=run_h265,
            run_av1=run_av1,
            run_4k=not args.skip_4k,
            run_throughput=run_throughput,
            run_quality=run_quality,
            run_extra_codecs=run_extra_codecs,
            run_extended_quality=run_extended_quality,
            strict=args.strict,
        )
    )
    try:
        exit_code = runner.run()
        if args.plot_charts:
            output_paths = render_summary_plots(
                runner.context.paths.summary_tsv,
                out_dir=args.plot_out_dir,
                title=args.plot_title,
            )
            for output_path in output_paths:
                print(f"图表: {output_path}")
        return exit_code
    except BenchmarkError as error:
        parser.exit(1, f"ERROR: {error}\n")
