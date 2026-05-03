import pytest

from rk3588_ai_video_codec.cli import build_parser, main
from rk3588_ai_video_codec.domain import (
    bitrate_to_bps,
    bitrate_to_kbps,
    build_extra_quality_cases,
    build_quality_cases,
    format_case_name,
    parse_psnr_average,
    parse_ssim_all,
)
from rk3588_ai_video_codec.process import (
    calc_cpu_pct,
    calc_fps,
    calc_realtime,
    sanitize_field,
)
from rk3588_ai_video_codec.runtime import BenchmarkConfig, prepare_context


def test_calc_fps() -> None:
    assert calc_fps(120, 2.0) == "60.0"


def test_calc_realtime() -> None:
    assert calc_realtime("120.0", 60) == "2.00"


def test_calc_cpu_pct() -> None:
    assert calc_cpu_pct(1.0, 0.5, 0.5) == "300%"


def test_sanitize_field() -> None:
    assert sanitize_field("a\tb\nc") == "a b c"


def test_format_case_name() -> None:
    assert format_case_name("640x360", 30) == "360p30"
    assert format_case_name("123x456", 24) == "123x456_24fps"


def test_parse_psnr_average() -> None:
    log_text = (
        "[Parsed_psnr_0 @ 0x1] PSNR y:37.12 u:35.20 v:35.02 average:36.21 "
        "min:35.90 max:36.40"
    )

    assert parse_psnr_average(log_text) == "36.21"


def test_parse_ssim_all() -> None:
    log_text = (
        "[Parsed_ssim_0 @ 0x1] SSIM Y:0.972030 U:0.981020 V:0.980010 "
        "All:0.975120 (16.038091)"
    )

    assert parse_ssim_all(log_text) == ("0.975120", "16.038091")


def test_build_quality_cases() -> None:
    cases = build_quality_cases()

    assert [case.case_name for case in cases] == [
        "360p30_500kbps",
        "480p30_1000kbps",
        "720p30_1500kbps",
        "1080p30_2500kbps",
        "1080p30_3500kbps",
    ]
    assert cases[1].size == "848x480"


def test_build_extra_quality_cases() -> None:
    assert [case.case_name for case in build_extra_quality_cases("VP9")] == [
        "360p30_500kbps",
        "480p30_1000kbps",
        "720p30_1500kbps",
    ]
    assert [case.case_name for case in build_extra_quality_cases("AV1")] == [
        "360p30_500kbps",
        "720p30_1500kbps",
    ]


def test_bitrate_to_units() -> None:
    assert bitrate_to_kbps("500k") == 500
    assert bitrate_to_kbps("2.5M") == 2500
    assert bitrate_to_bps("1500k") == 1_500_000


def test_build_parser_accepts_quality_only() -> None:
    args = build_parser().parse_args(["--quality-only", "--h264-only"])

    assert args.quality_only is True
    assert args.h264_only is True


def test_build_parser_accepts_plot_summary(tmp_path) -> None:
    args = build_parser().parse_args(["--plot-summary", str(tmp_path / "summary.tsv")])

    assert args.plot_summary == tmp_path / "summary.tsv"


def test_build_parser_accepts_quality_extra_codecs() -> None:
    args = build_parser().parse_args(["--quality-extra-codecs"])

    assert args.quality_extra_codecs is True


def test_prepare_context_splits_config_paths_and_state(tmp_path) -> None:
    out_dir = tmp_path / "result"

    context = prepare_context(BenchmarkConfig(profile="quick", out_dir=out_dir))

    assert context.config.out_dir == out_dir
    assert context.paths.out_dir == out_dir
    assert context.paths.summary_tsv.exists()
    assert context.paths.summary_tsv.read_text(encoding="utf-8").startswith(
        "status\tcodec\ttype\tbackend\tcase\t"
    )
    assert context.state.rows == []
    assert context.state.fail_count == 0
    assert context.state.unavailable_count == 0


def test_main_rejects_quality_with_av1_only() -> None:
    with pytest.raises(SystemExit) as error:
        main(["--quality-only", "--av1-only"])

    assert error.value.code == 2


def test_main_rejects_quality_extra_codecs_with_codec_only() -> None:
    with pytest.raises(SystemExit) as error:
        main(["--quality-extra-codecs", "--av1-only"])

    assert error.value.code == 2
