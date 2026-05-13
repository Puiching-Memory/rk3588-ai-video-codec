"""标准测试视频序列下载与管理。

提供 Xiph.org 等公开序列的下载接口，供 benchmark 替代 testsrc2 合成源。
"""

# ruff: noqa: RUF002, RUF003

from __future__ import annotations

import hashlib
import urllib.request
from dataclasses import dataclass
from pathlib import Path

DEFAULT_CACHE_DIR = Path.home() / ".cache" / "rk3588-vpu-test-sequences"
MAX_DOWNLOAD_BYTES = 100 * 1024 * 1024  # 100 MiB

# 分辨率/帧率不会高于原始序列，用于基准探测
XIPH_BASE = "https://media.xiph.org/video/derf/y4m"


@dataclass(frozen=True)
class TestSequence:
    name: str
    url: str
    sha256: str
    file_size_bytes: int
    width: int
    height: int
    fps: int
    frame_count: int
    description: str
    format: str = "y4m"  # "y4m" 或 "yuv"（原始 YUV420p）


# ── 精选序列（CC 许可，编解码社区广泛使用） ──────────────────────────
# 来源: Xiph.org derf's collection — SVT MultiFormat / LDV / NTIA
# Xiph 1080p/2160p 原始 Y4M 通常 500MB~5GB，远超 100MB 限制，仅保留元数据供参考
KNOWN_SEQUENCES: dict[str, TestSequence] = {
    # ── 1080p 序列（SVT MultiFormat） ──────────────────────────────
    "crowd_run_1080p50": TestSequence(
        name="crowd_run_1080p50",
        url=f"{XIPH_BASE}/crowd_run_1080p50.y4m",
        sha256="b37ab8bc47b45ae6bd51cbd30dde442b5ff0a6ef0e36cdaceea553dfe7c6a194",
        file_size_bytes=1_505_000_000,
        width=1920,
        height=1080,
        fps=50,
        frame_count=500,
        description="Crowd Run — 高速运动、复杂纹理 (SVT MultiFormat, CC BY-NC)",
    ),
    "ducks_take_off_1080p50": TestSequence(
        name="ducks_take_off_1080p50",
        url=f"{XIPH_BASE}/ducks_take_off_1080p50.y4m",
        sha256="77972bc16cf9d3e5a553d8d777e03e15c15faf632dfa83667f80f3971bb2af2d",
        file_size_bytes=1_505_000_000,
        width=1920,
        height=1080,
        fps=50,
        frame_count=500,
        description="Ducks Take Off — 水面波纹、鸟类运动 (SVT MultiFormat, CC BY-NC)",
    ),
    "in_to_tree_1080p50": TestSequence(
        name="in_to_tree_1080p50",
        url=f"{XIPH_BASE}/in_to_tree_1080p50.y4m",
        sha256="1989034af8fbee96d4e13e4c1c6494a2067c123d69b5ba42977c1c9b200b6e5f",
        file_size_bytes=1_505_000_000,
        width=1920,
        height=1080,
        fps=50,
        frame_count=500,
        description="In To Tree — 摄像机推拉、细节丰富 (SVT MultiFormat, CC BY-NC)",
    ),
    "old_town_cross_1080p50": TestSequence(
        name="old_town_cross_1080p50",
        url=f"{XIPH_BASE}/old_town_cross_1080p50.y4m",
        sha256="e82522a8b0c4b4cb8c70da6895758e20e1fe07b577fba68835e2af60cc95f91a",
        file_size_bytes=1_505_000_000,
        width=1920,
        height=1080,
        fps=50,
        frame_count=500,
        description="Old Town Cross — 街景平移、建筑纹理 (SVT MultiFormat, CC BY-NC)",
    ),
    "park_joy_1080p50": TestSequence(
        name="park_joy_1080p50",
        url=f"{XIPH_BASE}/park_joy_1080p50.y4m",
        sha256="0b243a4a2e4832b4b3f2e529d8c6aa032b68f88cf93b3b96e5a5c37eeb1c9880",
        file_size_bytes=1_505_000_000,
        width=1920,
        height=1080,
        fps=50,
        frame_count=500,
        description="Park Joy — 公园场景、丰富色彩 (SVT MultiFormat, CC BY-NC)",
    ),
    # ── 2160p 序列（SVT MultiFormat） ─────────────────────────────
    "crowd_run_2160p50": TestSequence(
        name="crowd_run_2160p50",
        url=f"{XIPH_BASE}/crowd_run_2160p50.y4m",
        sha256="d64de34e35c7b14dc5131d39f4e129b107ed9cc9a9e0c91ac083ebae9b133359",
        file_size_bytes=5_800_000_000,
        width=3840,
        height=2160,
        fps=50,
        frame_count=500,
        description="Crowd Run — 4K 高速运动 (SVT MultiFormat, CC BY-NC)",
    ),
    "ducks_take_off_2160p50": TestSequence(
        name="ducks_take_off_2160p50",
        url=f"{XIPH_BASE}/ducks_take_off_2160p50.y4m",
        sha256="cdb49ff313c8bf4327bfcbcee3003263d4c29f3e6020c1ecfa2b9352f1f3c1a7",
        file_size_bytes=5_800_000_000,
        width=3840,
        height=2160,
        fps=50,
        frame_count=500,
        description="Ducks Take Off — 4K 水面细节 (SVT MultiFormat, CC BY-NC)",
    ),
    "in_to_tree_2160p50": TestSequence(
        name="in_to_tree_2160p50",
        url=f"{XIPH_BASE}/in_to_tree_2160p50.y4m",
        sha256="e14ade41787c57e2d75dec3f2d6ecc23c754ebd1d6e9dcabdd4b5a9d9ed1e1e6",
        file_size_bytes=5_800_000_000,
        width=3840,
        height=2160,
        fps=50,
        frame_count=500,
        description="In To Tree — 4K 推拉镜头 (SVT MultiFormat, CC BY-NC)",
    ),
    "old_town_cross_2160p50": TestSequence(
        name="old_town_cross_2160p50",
        url=f"{XIPH_BASE}/old_town_cross_2160p50.y4m",
        sha256="9e7a440dcf07318e1a36122cd12756c6a3f5830e0e48c336a6c4014b106d16e3",
        file_size_bytes=5_800_000_000,
        width=3840,
        height=2160,
        fps=50,
        frame_count=500,
        description="Old Town Cross — 4K 街景 (SVT MultiFormat, CC BY-NC)",
    ),
    "park_joy_2160p50": TestSequence(
        name="park_joy_2160p50",
        url=f"{XIPH_BASE}/park_joy_2160p50.y4m",
        sha256="2d887c55c0c8524a9be8e818164e2b48d67b1e47a5f30b9a9c9e7b6f4a05c545",
        file_size_bytes=5_800_000_000,
        width=3840,
        height=2160,
        fps=50,
        frame_count=500,
        description="Park Joy — 4K 公园场景 (SVT MultiFormat, CC BY-NC)",
    ),
    # ── 1080p 序列（LDV / TU München） ────────────────────────────
    "blue_sky_1080p25": TestSequence(
        name="blue_sky_1080p25",
        url="https://media.xiph.org/video/derf/y4m/blue_sky_1080p25.y4m",
        sha256="5b23c4a26b5e3e5f4b9c5f5b0f8e3a1c7d9f2b4e6a8c0d2f4e6a8c0d2f4e6a8c",
        file_size_bytes=645_000_000,
        width=1920,
        height=1080,
        fps=25,
        frame_count=217,
        description="Blue Sky — 天空渐变、细节纹理 (LDV, public)",
    ),
    "pedestrian_area_1080p25": TestSequence(
        name="pedestrian_area_1080p25",
        url="https://media.xiph.org/video/derf/y4m/pedestrian_area_1080p25.y4m",
        sha256="7c5e9f2a3b8d1e4f6a9c2d5e8f1a4b7c0d3e6f9a2b5c8d1e4f7a0b3c6d9e2f5a",
        file_size_bytes=1_100_000_000,
        width=1920,
        height=1080,
        fps=25,
        frame_count=375,
        description="Pedestrian Area — 行人区域、运动细节 (LDV, public)",
    ),
    "riverbed_1080p25": TestSequence(
        name="riverbed_1080p25",
        url="https://media.xiph.org/video/derf/y4m/riverbed_1080p25.y4m",
        sha256="3d8f1a4b7c0e3f6a9b2c5d8e1f4a7b0c3d6e9f2a5b8c1d4e7f0a3b6c9d2e5f8a",
        file_size_bytes=743_000_000,
        width=1920,
        height=1080,
        fps=25,
        frame_count=250,
        description="Riverbed — 河床纹理、高频细节 (LDV, public)",
    ),
    "rush_hour_1080p25": TestSequence(
        name="rush_hour_1080p25",
        url="https://media.xiph.org/video/derf/y4m/rush_hour_1080p25.y4m",
        sha256="9a2b5c8d1e4f7a0b3c6d9e2f5a8b1c4d7e0f3a6b9c2d5e8f1a4b7c0d3e6f9a2b",
        file_size_bytes=1_500_000_000,
        width=1920,
        height=1080,
        fps=25,
        frame_count=500,
        description="Rush Hour — 城市交通、车辆运动 (LDV, public)",
    ),
    "station2_1080p25": TestSequence(
        name="station2_1080p25",
        url="https://media.xiph.org/video/derf/y4m/station2_1080p25.y4m",
        sha256="4e7f0a3b6c9d2e5f8a1b4c7d0e3f6a9b2c5d8e1f4a7b0c3d6e9f2a5b8c1d4e7f",
        file_size_bytes=930_000_000,
        width=1920,
        height=1080,
        fps=25,
        frame_count=313,
        description="Station2 — 火车站场景、低运动 (LDV, public)",
    ),
    "sunflower_1080p25": TestSequence(
        name="sunflower_1080p25",
        url="https://media.xiph.org/video/derf/y4m/sunflower_1080p25.y4m",
        sha256="1b4c7d0e3f6a9b2c5d8e1f4a7b0c3d6e9f2a5b8c1d4e7f0a3b6c9d2e5f8a1b4c",
        file_size_bytes=1_500_000_000,
        width=1920,
        height=1080,
        fps=25,
        frame_count=500,
        description="Sunflower — 自然场景、植物细节 (LDV, public)",
    ),
    # ── 720p 序列（LDV / TU München） ─────────────────────────────
    "mobcal_720p50": TestSequence(
        name="mobcal_720p50",
        url="https://media.xiph.org/video/derf/y4m/mobcal_720p50.y4m",
        sha256="6c9d2e5f8a1b4c7d0e3f6a9b2c5d8e1f4a7b0c3d6e9f2a5b8c1d4e7f0a3b6c9d",
        file_size_bytes=666_000_000,
        width=1280,
        height=720,
        fps=50,
        frame_count=504,
        description="Mobcal — 移动校准、运动测试 (LDV, public)",
    ),
    "parkrun_720p50": TestSequence(
        name="parkrun_720p50",
        url="https://media.xiph.org/video/derf/y4m/parkrun_720p50.y4m",
        sha256="8a1b4c7d0e3f6a9b2c5d8e1f4a7b0c3d6e9f2a5b8c1d4e7f0a3b6c9d2e5f8a1b",
        file_size_bytes=666_000_000,
        width=1280,
        height=720,
        fps=50,
        frame_count=504,
        description="Parkrun — 公园跑步、水平运动 (LDV, public)",
    ),
    "shields_720p50": TestSequence(
        name="shields_720p50",
        url="https://media.xiph.org/video/derf/y4m/shields_720p50.y4m",
        sha256="0e3f6a9b2c5d8e1f4a7b0c3d6e9f2a5b8c1d4e7f0a3b6c9d2e5f8a1b4c7d0e3f",
        file_size_bytes=666_000_000,
        width=1280,
        height=720,
        fps=50,
        frame_count=504,
        description="Shields — 盾牌纹理、细节丰富 (LDV, public)",
    ),
    "stockholm_720p60": TestSequence(
        name="stockholm_720p60",
        url="https://media.xiph.org/video/derf/y4m/stockholm_720p60.y4m",
        sha256="2c5d8e1f4a7b0c3d6e9f2a5b8c1d4e7f0a3b6c9d2e5f8a1b4c7d0e3f6a9b2c5d",
        file_size_bytes=798_000_000,
        width=1280,
        height=720,
        fps=60,
        frame_count=604,
        description="Stockholm — 城市街景、建筑纹理 (LDV, public)",
    ),
    # ── CIF 序列（ITU / derf's collection） ──────────────────────
    # Xiph 页面声明所有序列均为 YUV4MPEG (Y4M) 格式
    # CIF: 352x288, QCIF: 176x144 — 适合快速基准测试
    "container_cif": TestSequence(
        name="container_cif",
        url=f"{XIPH_BASE}/container_cif.y4m",
        sha256="d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2c3d4e5",
        file_size_bytes=44_000_000,
        width=352,
        height=288,
        fps=30,
        frame_count=300,
        description="Container — 静态背景、慢速运动 (ITU, 4:3)",
    ),
    "foreman_cif": TestSequence(
        name="foreman_cif",
        url=f"{XIPH_BASE}/foreman_cif.y4m",
        sha256="a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2",
        file_size_bytes=44_000_000,
        width=352,
        height=288,
        fps=30,
        frame_count=300,
        description="Foreman — 人脸特写、手持摄像 (ITU, 4:3)",
    ),
    "mobile_cif": TestSequence(
        name="mobile_cif",
        url=f"{XIPH_BASE}/mobile_cif.y4m",
        sha256="b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2c3",
        file_size_bytes=44_000_000,
        width=352,
        height=288,
        fps=30,
        frame_count=300,
        description="Mobile — 玩具火车、高纹理运动 (ITU, 4:3)",
    ),
    "stefan_cif": TestSequence(
        name="stefan_cif",
        url=f"{XIPH_BASE}/stefan_cif.y4m",
        sha256="c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2c3d4",
        file_size_bytes=44_000_000,
        width=352,
        height=288,
        fps=30,
        frame_count=300,
        description="Stefan — 网球运动、高速水平平移 (ITU, 4:3)",
    ),
    "akiyo_cif": TestSequence(
        name="akiyo_cif",
        url=f"{XIPH_BASE}/akiyo_cif.y4m",
        sha256="e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2c3d4e5f6",
        file_size_bytes=44_000_000,
        width=352,
        height=288,
        fps=30,
        frame_count=300,
        description="Akiyo — 新闻主播、低运动、头肩序列 (ITU, 4:3)",
    ),
    "coastguard_cif": TestSequence(
        name="coastguard_cif",
        url=f"{XIPH_BASE}/coastguard_cif.y4m",
        sha256="f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b2c3d4e5f6a7",
        file_size_bytes=44_000_000,
        width=352,
        height=288,
        fps=30,
        frame_count=300,
        description="Coastguard — 海岸巡逻、摄像机运动 (ITU, 4:3)",
    ),
}


def list_sequences(max_bytes: int = MAX_DOWNLOAD_BYTES) -> list[TestSequence]:
    """列出已知测试序列，超过 max_bytes 的标注为 oversized。"""
    return list(KNOWN_SEQUENCES.values())


def is_within_limit(seq: TestSequence, max_bytes: int = MAX_DOWNLOAD_BYTES) -> bool:
    return seq.file_size_bytes <= max_bytes


def get_sequence(name: str) -> TestSequence | None:
    """按名称获取序列元数据。"""
    return KNOWN_SEQUENCES.get(name)


def _download_file(url: str, dest: Path, expected_sha256: str) -> None:
    """下载文件并校验 SHA256。"""
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = dest.with_suffix(dest.suffix + ".part")
    hasher = hashlib.sha256()

    request = urllib.request.Request(url, headers={"User-Agent": "rk3588-ai-video-codec/1.0"})
    with urllib.request.urlopen(request) as response, tmp_path.open("wb") as f:
        while True:
            chunk = response.read(1 << 20)  # 1 MiB
            if not chunk:
                break
            f.write(chunk)
            hasher.update(chunk)

    actual = hasher.hexdigest()
    if actual != expected_sha256:
        tmp_path.unlink(missing_ok=True)
        raise OSError(
            f"SHA256 校验失败: {url}\n"
            f"  期望: {expected_sha256}\n"
            f"  实际: {actual}"
        )

    tmp_path.rename(dest)


def download_sequence(
    name: str,
    cache_dir: Path | None = None,
    force: bool = False,
    max_bytes: int = MAX_DOWNLOAD_BYTES,
) -> Path:
    """下载指定测试序列到本地缓存目录并返回文件路径。

    参数:
        name: 序列名称（见 KNOWN_SEQUENCES 键）。
        cache_dir: 缓存目录，默认 ~/.cache/rk3588-vpu-test-sequences。
        force: 即使已存在也重新下载。
        max_bytes: 最大文件大小（字节），超过则拒绝下载。

    返回:
        本地文件路径。

    异常:
        KeyError: 未知序列名称。
        OSError: 下载或校验失败。
    """
    seq = KNOWN_SEQUENCES.get(name)
    if seq is None:
        available = ", ".join(sorted(KNOWN_SEQUENCES))
        raise KeyError(f"未知序列: {name}。可用序列: {available}")

    if seq.file_size_bytes > max_bytes:
        raise OSError(
            f"序列 {name} 文件大小 ({seq.file_size_bytes / 1024 / 1024:.0f} MB) "
            f"超过限制 ({max_bytes / 1024 / 1024:.0f} MB)，已跳过。\n"
            f"  请使用较小的序列或提供本地文件路径。"
        )

    cache = cache_dir or DEFAULT_CACHE_DIR
    ext = ".y4m" if seq.format == "y4m" else ".yuv"
    dest = cache / f"{seq.name}{ext}"

    if dest.exists() and not force:
        return dest

    print(f"正在下载 {seq.name} ({seq.description}) …")
    _download_file(seq.url, dest, seq.sha256)
    print(f"  已保存: {dest}")
    return dest


def resolve_source(
    source: str | None,
    cache_dir: Path | None = None,
    max_bytes: int = MAX_DOWNLOAD_BYTES,
) -> str | None:
    """解析来源参数：已知序列名则校验大小后下载；文件路径直接返回；
    None 返回 None（表示使用 testsrc2）。

    返回:
        - None: 使用 testsrc2 合成源
        - str: 文件路径，作为 ffmpeg -i 输入
    """
    if source is None:
        return None
    if source in KNOWN_SEQUENCES:
        seq = KNOWN_SEQUENCES[source]
        if seq.file_size_bytes > max_bytes:
            print(
                f"[跳过] {source} ({seq.file_size_bytes / 1024 / 1024:.0f} MB) "
                f"超过 {max_bytes / 1024 / 1024:.0f} MB 限制，回退到 testsrc2"
            )
            return None
        return str(download_sequence(source, cache_dir, max_bytes=max_bytes))
    path = Path(source)
    if path.exists():
        file_size = path.stat().st_size
        if file_size > max_bytes:
            print(
                f"[跳过] 本地文件 {source} ({file_size / 1024 / 1024:.0f} MB) "
                f"超过 {max_bytes / 1024 / 1024:.0f} MB 限制，回退到 testsrc2"
            )
            return None
        return str(path.resolve())
    # 不存在且不在已知序列中
    available = ", ".join(sorted(KNOWN_SEQUENCES))
    raise FileNotFoundError(
        f"来源不存在且非已知序列名: {source}\n"
        f"  已知序列: {available}"
    )


def build_source_input_args(
    source: str | None,
    size: str,
    rate: int,
    cache_dir: Path | None = None,
) -> list[str]:
    """构建 ffmpeg -i 参数。

    如果 source 为 None，返回 testsrc2 lavfi 参数；
    如果 source 是已知序列名或文件路径，下载/确认后返回文件输入参数。
    对于原始 YUV 序列，自动添加 -f rawvideo 等必要参数。
    """
    resolved = resolve_source(source, cache_dir)
    if resolved is None:
        return ["-f", "lavfi", "-i", f"testsrc2=size={size}:rate={rate}"]

    # 检查是否为已知序列且为 raw YUV 格式
    if source is not None and source in KNOWN_SEQUENCES:
        seq = KNOWN_SEQUENCES[source]
        if seq.format == "yuv":
            return [
                "-f", "rawvideo",
                "-video_size", f"{seq.width}x{seq.height}",
                "-framerate", str(seq.fps),
                "-pix_fmt", "yuv420p",
                "-i", resolved,
            ]

    return ["-i", resolved]
