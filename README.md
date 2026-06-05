# rk3588-ai-video-codec

面向 RK3588 的 H.265 硬件视频编解码 C 库及基准测试工具，基于 [ffmpeg-rockchip](https://github.com/nyanmisaka/ffmpeg-rockchip) 的 RKMPP 硬件加速。

## 功能

- **H.265 硬件编码** — RKMPP 加速，支持 8K 分辨率，异步编码
- **H.265 硬件解码** — RKMPP 加速，支持 8K 10-bit 解码
- **离线处理** — 文件输入/输出，自动 muxer/demuxer
- **实时流式处理** — 帧级 push/pull API，适合监控/推流场景
- **零拷贝 DMA** — 硬件帧在 VPU 和 RGA 之间零拷贝传递

## 性能 (RK3588, 1080p)

| 测试类型   | 帧率     | 实时倍率 |
| ---------- | -------- | -------- |
| H.265 编码 | ~290 fps | ~9.6x    |
| H.265 解码 | ~270 fps | ~9.0x    |
| 流式编码   | ~215 fps | ~7.2x    |

## 端到端延迟 (RK3588, 1080p, 低延迟模式)

| 指标           | 编码延迟 | 端到端延迟 |
| -------------- | -------- | ---------- |
| 平均           | ~7 ms    | ~69 ms     |
| P50            | ~7 ms    | ~76 ms     |
| P95            | ~8 ms    | ~84 ms     |

## 快速开始 (C 库)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 运行基准测试
./rkvc_bench --quick

# 安装
cmake --install . --prefix /usr/local
```

## 严格测试

本项目把测试分成可重复执行的多层门禁，思路参考 SQLite：确定性契约测试、OOM/I/O 异常路径、ASan/UBSan 动态分析、覆盖率度量和实机交付验证。

```bash
# 基线单元测试
cmake --preset tests
cmake --build --preset tests
ctest --preset tests

# ASan + UBSan
cmake --preset asan
cmake --build --preset asan
ctest --preset asan

# 覆盖率构建
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage

# 一键严格模式
./scripts/test-strict.sh

# RK3588 实机覆盖率阈值（需要 gcovr）
RKVC_COVERAGE_MIN_LINE=80 RKVC_COVERAGE_MIN_BRANCH=70 ./scripts/test-strict.sh
```

`test_hardware` 会自动探测 RKMPP 设备节点；在非实机环境中跳过，在 RK3588 实机上执行真实 H.265 硬件编码/解码往返。

更详细的测试策略见 [docs/testing.md](docs/testing.md)。

### 依赖

- Rockchip BSP 内核 (5.10 或 6.1)
- [ffmpeg-rockchip](https://github.com/nyanmisaka/ffmpeg-rockchip) (`/usr/local/ffmpeg-rockchip`)
- libdrm-dev
- CMake >= 3.16
- CMocka (单元测试)

### 设备权限

```bash
sudo chmod 666 /dev/mpp_service /dev/dma_heap /dev/rga
sudo chmod 666 /dev/dri/*
```

## C API 用法示例

```c
#include "rkvc/rkvc.h"

rkvc_init();

// 编码
rkvc_encoder *enc = NULL;
rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
cfg.width = 1920; cfg.height = 1080;
cfg.bitrate = 4000000;
rkvc_encoder_open_file(&enc, &cfg, "output.h265");

rkvc_frame *f = NULL;
rkvc_frame_alloc(&f, 1920, 1080, RKVC_PIX_FMT_NV12);
// 填充 NV12 像素数据到 f ...
rkvc_encoder_send_frame(enc, f);
rkvc_frame_unref(f);
rkvc_encoder_close(enc);

// 解码
rkvc_decoder *dec = NULL;
rkvc_decoder_config dcfg = rkvc_decoder_config_defaults();
rkvc_decoder_open_file(&dec, &dcfg, "output.h265");
rkvc_frame *frame = NULL;
while (rkvc_decoder_read_packet(dec) == RKVC_OK)
    while (rkvc_decoder_receive_frame(dec, &frame) == RKVC_OK)
        rkvc_frame_unref(frame);
rkvc_decoder_close(dec);

rkvc_deinit();
```

### 项目结构

```
include/rkvc/         # 公共 API 头文件
    rkvc.h            # 主入口 (包含所有子头文件)
    types.h           # 错误码、像素格式、配置
    frame.h           # 帧管理
    encoder.h         # H.265 编码器
    decoder.h         # H.265 解码器
    stream.h          # 实时流式处理
lib/                  # 库实现
bench/                # 基准测试工具
examples/             # 示例程序 (encode_file, decode_file, transcode, stream_*, latency_test, psnr_test)
```

## 基准测试

```bash
cd build
./rkvc_bench --quick          # 快速测试 (120 帧)
./rkvc_bench                  # 完整测试 (300 帧)
./rkvc_bench --4k             # 4K 测试
./rkvc_bench --stream         # 包含流式 API 测试
./rkvc_bench --encode-only    # 仅编码测试
./rkvc_bench -o results/run1  # 指定输出目录
```

### 编码能力矩阵

当前项目只启用 HEVC 路径，FFmpeg 构建也按此裁剪：

| 路径          | 编码           | 解码           | 备注                       |
| ------------- | -------------- | -------------- | -------------------------- |
| HEVC 硬件路径 | `hevc_rkmpp` ✅ | `hevc_rkmpp` ✅ | 编解码均走 RKMPP           |
| HEVC 软件回退 | 未启用         | FFmpeg HEVC ✅  | 精简配置仅保留软件解码回退 |

## 致谢

- [ffmpeg-rockchip](https://github.com/nyanmisaka/ffmpeg-rockchip) — 为 Rockchip SoC 提供硬件加速编解码的 FFmpeg 分支，本项目依赖其 RKMPP 编解码器。
