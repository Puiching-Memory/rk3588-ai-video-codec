from rk3588_ai_video_codec.benchmark import calc_cpu_pct, calc_fps, calc_realtime, sanitize_field


def test_calc_fps() -> None:
    assert calc_fps(120, 2.0) == "60.0"


def test_calc_realtime() -> None:
    assert calc_realtime("120.0", 60) == "2.00"


def test_calc_cpu_pct() -> None:
    assert calc_cpu_pct(1.0, 0.5, 0.5) == "300%"


def test_sanitize_field() -> None:
    assert sanitize_field("a\tb\nc") == "a b c"
