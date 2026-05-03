from __future__ import annotations

from dataclasses import dataclass, field

from .ffmpeg_backend import FFMPEG_BIN, FFPROBE_BIN
from .process import command_exists
from .reporting import render_summary_md, write_system_info
from .runtime import BenchmarkConfig, BenchmarkContext, prepare_context
from .suites.extra_codecs import (
    run_av1_tests,
    run_extra_codec_suites,
)
from .suites.h26x import (
    run_h26x_quality_suite,
    run_h26x_suite,
)
from .suites.quality import run_extended_quality_suites


class BenchmarkError(RuntimeError):
    pass


@dataclass
class BenchmarkRunner:
    config: BenchmarkConfig
    context: BenchmarkContext = field(init=False)

    def __post_init__(self) -> None:
        self.context = prepare_context(self.config)

    def ensure_requirements(self) -> None:
        required = [FFMPEG_BIN, FFPROBE_BIN]
        missing = [name for name in required if not command_exists(name)]
        if missing:
            raise BenchmarkError(f"缺少命令: {', '.join(missing)}")

    def run(self) -> int:
        context = self.context
        config = context.config
        self.ensure_requirements()
        write_system_info(context)

        if config.run_h265:
            if config.run_throughput:
                run_h26x_suite(context, "H265", "hevc_rkmpp", "hevc", "hevc")
            if config.run_quality:
                run_h26x_quality_suite(context, "H265", "hevc_rkmpp", "hevc", "hevc")

        if config.run_h264:
            if config.run_throughput:
                run_h26x_suite(context, "H264", "h264_rkmpp", "h264", "h264")
            if config.run_quality:
                run_h26x_quality_suite(context, "H264", "h264_rkmpp", "h264", "h264")

        if config.run_av1 and config.run_throughput:
            run_av1_tests(context)

        if config.run_throughput and config.run_extra_codecs:
            run_extra_codec_suites(context)

        if config.run_extended_quality:
            run_extended_quality_suites(context)

        render_summary_md(context)

        print()
        print(f"结果目录: {context.paths.out_dir}")
        print(f"汇总文件: {context.paths.summary_tsv}")
        print(f"说明文件: {context.paths.summary_md}")

        if config.strict and (
            context.state.fail_count > 0 or context.state.unavailable_count > 0
        ):
            return 1
        return 0
