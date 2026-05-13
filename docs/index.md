# rkvc — RK3588 H.265 视频编解码库

基于 [ffmpeg-rockchip](https://github.com/nyanmisaka/ffmpeg-rockchip) 的 RKMPP 硬件加速，为 RK3588 提供高性能 H.265 编解码 C API。

## 功能特性

- **H.265 硬件编码** — RKMPP 加速，支持 8K 分辨率，异步编码
- **H.265 硬件解码** — RKMPP 加速，支持 8K 10-bit 解码
- **离线处理** — 文件输入/输出，自动 muxer/demuxer
- **实时流式处理** — 帧级 push/pull API，适合监控/推流场景
- **零拷贝 DMA** — 硬件帧在 VPU 和 RGA 之间零拷贝传递

## 性能

| 测试类型   | 帧率     | 实时倍率 |
| ---------- | -------- | -------- |
| H.265 编码 | ~290 fps | ~9.6x    |
| H.265 解码 | ~270 fps | ~9.0x    |
| 流式编码   | ~215 fps | ~7.2x    |

## 导航

- [快速开始](getting-started.md) — 构建、安装、首次运行
- [API 参考](api.md) — 完整 API 文档
- [架构](architecture.md) — 内部设计和模块关系
- [基准测试](benchmark.md) — 性能测试方法和结果
