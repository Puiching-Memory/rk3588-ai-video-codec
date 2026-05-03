from pathlib import Path

import pytest

from rk3588_ai_video_codec.plotting import (
    load_summary_points,
    render_summary_plots,
    resolve_summary_path,
    select_quality_points,
    select_runtime_points,
)

SAMPLE_SUMMARY = "\n".join(
    [
        (
            "status\tcodec\ttype\tbackend\tcase\tsize\trate\tframes\telapsed_s\tfps\t"
            "realtime\tcpu\tnote\tartifact"
        ),
        (
            "PASS\tH265\tquality\tffmpeg\t360p30_500kbps\t640x360\t30\t120\t0.637\t188.4\t"
            "6.28\t207%\ttarget=500k avg_kbps=498.8 "
            "psnr_avg=39.213216 ssim_all=0.984882 "
            "ssim_db=18.205161\t/tmp/h265.hevc"
        ),
        (
            "PASS\tH264\tquality\tffmpeg\t360p30_500kbps\t640x360\t30\t120\t0.403\t297.5\t"
            "9.92\t274%\ttarget=500k avg_kbps=546.8 "
            "psnr_avg=35.192434 ssim_all=0.969317 "
            "ssim_db=15.131044\t/tmp/h264.h264"
        ),
        (
            "PASS\tMJPEG\tencode\tffmpeg\t360p30\t640x360\t30\t30\t0.100\t300.0\t10.00\t"
            "120%\tavg_mbps=4.50\t/tmp/mjpeg.mjpeg"
        ),
    ]
) + "\n"

MISSING_BACKEND_SUMMARY = "\n".join(
    [
        (
            "status\tcodec\ttype\tbackend\tcase\tsize\trate\tframes\telapsed_s\tfps\t"
            "realtime\tcpu\tnote\tartifact"
        ),
        (
            "PASS\tVP9\tdecode\t\t360p30\t640x360\t30\t30\t0.200\t150.0\t5.00\t80%\t"
            "sample_encoder=libvpx-vp9 decoder=vp9_rkmpp\t/tmp/vp9.webm"
        ),
    ]
) + "\n"


def write_summary(path: Path) -> Path:
    path.write_text(SAMPLE_SUMMARY, encoding="utf-8")
    return path


def test_resolve_summary_path_accepts_result_dir(tmp_path: Path) -> None:
    result_dir = tmp_path / "result"
    result_dir.mkdir()
    summary_path = write_summary(result_dir / "summary.tsv")

    assert resolve_summary_path(result_dir) == summary_path


def test_load_summary_points_parses_quality_metrics(tmp_path: Path) -> None:
    summary_path = write_summary(tmp_path / "summary.tsv")

    points = load_summary_points(summary_path)

    assert len(points) == 3
    assert points[0].codec == "H265"
    assert points[0].bitrate_kbps == pytest.approx(498.8)
    assert points[0].target_kbps == pytest.approx(500.0)
    assert points[0].backend == "ffmpeg"
    assert points[0].psnr_avg == pytest.approx(39.213216)
    assert points[0].latency_ms == pytest.approx((0.637 / 120) * 1000)
    assert points[1].cpu_pct == pytest.approx(274.0)
    assert points[2].bitrate_kbps == pytest.approx(4500.0)


def test_load_summary_points_defaults_backend_to_ffmpeg_when_missing(tmp_path: Path) -> None:
    summary_path = tmp_path / "summary.tsv"
    summary_path.write_text(MISSING_BACKEND_SUMMARY, encoding="utf-8")

    points = load_summary_points(summary_path)

    assert len(points) == 1
    assert points[0].backend == "ffmpeg"


def test_select_runtime_points_keeps_non_quality_series(tmp_path: Path) -> None:
    summary_path = write_summary(tmp_path / "summary.tsv")

    points = load_summary_points(summary_path)

    assert [point.codec for point in select_quality_points(points)] == ["H265", "H264"]
    assert [point.codec for point in select_runtime_points(points)] == ["H265", "H264", "MJPEG"]


def test_render_summary_plots_smoke(tmp_path: Path) -> None:
    pytest.importorskip("matplotlib")
    summary_path = write_summary(tmp_path / "summary.tsv")

    output_paths = render_summary_plots(summary_path, out_dir=tmp_path / "plots", title="Smoke")

    assert [path.name for path in output_paths] == [
        "rd_performance_latency_dashboard.png",
        "bitrate_vs_psnr.png",
        "bitrate_vs_ssim.png",
        "bitrate_vs_fps.png",
        "bitrate_vs_latency_ms.png",
    ]
    assert all(path.exists() for path in output_paths)
