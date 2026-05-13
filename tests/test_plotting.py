from pathlib import Path

import pytest

from rk3588_ai_video_codec.plotting import (
    load_summary_points,
    plot_bpp,
    render_summary_plots,
    resolve_summary_path,
    select_preview_points,
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
            "6.28\t207%\ttarget_bpp=0.0723 avg_bpp=0.0722 "
            "psnr_avg=39.213216 ssim_all=0.984882 "
            "ssim_db=18.205161\t/tmp/h265.hevc"
        ),
        (
            "PASS\tH264\tquality\tffmpeg\t360p30_500kbps\t640x360\t30\t120\t0.403\t297.5\t"
            "9.92\t274%\ttarget_bpp=0.0723 avg_bpp=0.0791 "
            "psnr_avg=35.192434 ssim_all=0.969317 "
            "ssim_db=15.131044\t/tmp/h264.h264"
        ),
        (
            "PASS\tVP9\tdecode\tffmpeg\t360p30\t640x360\t30\t30\t0.200\t150.0\t5.00\t"
            "80%\tsample_encoder=libvpx-vp9 decoder=vp9_rkmpp avg_bpp=0.2894\t/tmp/vp9.webm"
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
            "PASS\tAV1\tdecode\t\t360p30\t640x360\t30\t30\t0.200\t150.0\t5.00\t80%\t"
            "sample_encoder=libaom-av1 decoder=av1_rkmpp\t/tmp/av1.webm"
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
    assert points[0].bpp == pytest.approx(0.0722)
    assert points[0].target_bpp == pytest.approx(0.0723)
    assert points[0].backend == "ffmpeg"
    assert points[0].psnr_avg == pytest.approx(39.213216)
    assert points[0].latency_ms == pytest.approx((0.637 / 120) * 1000)
    assert points[1].cpu_pct == pytest.approx(274.0)
    assert points[2].bpp == pytest.approx(0.2894)


def test_plot_bpp_prefers_quality_target(tmp_path: Path) -> None:
    summary_path = write_summary(tmp_path / "summary.tsv")

    points = load_summary_points(summary_path)

    assert plot_bpp(points[0]) == pytest.approx(0.0723)
    assert plot_bpp(points[1]) == pytest.approx(0.0723)
    assert plot_bpp(points[2]) == pytest.approx(0.2894)


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
    assert [point.codec for point in select_runtime_points(points)] == ["H265", "H264", "VP9"]


def test_select_preview_points_prefers_100kbps(tmp_path: Path) -> None:
    summary_path = tmp_path / "summary.tsv"
    summary_path.write_text(
        "\n".join(
            [
                (
                    "status\tcodec\ttype\tbackend\tcase\tsize\trate\tframes\telapsed_s\tfps\t"
                    "realtime\tcpu\tnote\tartifact"
                ),
                (
                    "PASS\tH265\tquality\tffmpeg\t360p30_500kbps\t640x360\t30\t120\t0.637\t188.4\t"
                    "6.28\t207%\ttarget_bpp=0.0723 avg_bpp=0.0722 psnr_avg=39.2 ssim_all=0.98\t/tmp/h265-500.hevc"
                ),
                (
                    "PASS\tH265\tquality\tffmpeg\t1080p30_100kbps\t1920x1080\t30\t120\t0.637\t188.4\t"
                    "6.28\t207%\ttarget_bpp=0.0016 avg_bpp=0.0015 psnr_avg=33.1 ssim_all=0.95\t/tmp/h265-100.hevc"
                ),
                (
                    "PASS\tH264\tquality\tffmpeg\t1080p30_100kbps\t1920x1080\t30\t120\t0.403\t297.5\t"
                    "9.92\t274%\ttarget_bpp=0.0016 avg_bpp=0.0017 psnr_avg=30.5 ssim_all=0.92\t/tmp/h264-100.h264"
                ),
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    points = load_summary_points(summary_path)

    assert [(point.codec, point.case_name) for point in select_preview_points(points)] == [
        ("H264", "1080p30_100kbps"),
        ("H265", "1080p30_100kbps"),
    ]


def test_render_summary_plots_smoke(tmp_path: Path) -> None:
    pytest.importorskip("matplotlib")
    summary_path = write_summary(tmp_path / "summary.tsv")

    output_paths = render_summary_plots(summary_path, out_dir=tmp_path / "plots", title="Smoke")

    assert [path.name for path in output_paths] == [
        "rd_performance_latency_dashboard.png",
        "bpp_vs_psnr.png",
        "bpp_vs_ssim.png",
        "bpp_vs_fps.png",
        "bpp_vs_latency_ms.png",
    ]
    assert all(path.exists() for path in output_paths)


def test_render_summary_plots_appends_preview_paths(tmp_path: Path, monkeypatch) -> None:
    pytest.importorskip("matplotlib")
    summary_path = write_summary(tmp_path / "summary.tsv")
    preview_path = tmp_path / "plots" / "preview_h265_1080p30_100kbps.png"

    monkeypatch.setattr(
        "rk3588_ai_video_codec.plotting.render_preview_images",
        lambda _points, _out_dir: [preview_path],
    )

    output_paths = render_summary_plots(summary_path, out_dir=tmp_path / "plots", title="Smoke")

    assert preview_path in output_paths
