# rk3588-ai-video-codec
面向 RK3588 的 FFmpeg VPU 基准 CLI，聚焦硬件编解码吞吐、画质还原度和结果可视化。

主入口是 `uv run benchmark-vpu`，源码位于 `src/rk3588_ai_video_codec/`。`scripts/benchmark_vpu.sh` 仍保留为兼容包装层。当前实现统一走 FFmpeg CLI，不再包含 GStreamer backend。

## 快速开始

安装依赖：

```bash
uv sync --dev
```

执行一次快速冒烟：

```bash
uv run benchmark-vpu --profile quick
```

追加 100 kbps 单档位画质测试：

```bash
uv run benchmark-vpu --profile quick --quality-ladder
```

对已有结果目录补绘图表与左右预览：

```bash
uv run benchmark-vpu --plot-summary results/<run-dir>
```

## RK3588 VPU 编解码能力矩阵

基准工具依赖 ffmpeg-rockchip（`--enable-rkmpp`），底层通过 RK MPP 调用 VPU 硬件。各编码器的硬加速支持如下：

| 格式  | 硬编码          | 硬解码          | 备注                                          |
| ----- | --------------- | --------------- | --------------------------------------------- |
| H264  | `h264_rkmpp` ✅  | `h264_rkmpp` ✅  | 编解码均走 RKMPP                              |
| H265  | `hevc_rkmpp` ✅  | `hevc_rkmpp` ✅  | 编解码均走 RKMPP                              |
| MJPEG | `mjpeg_rkmpp` ✅ | `mjpeg_rkmpp` ✅ | 编解码均走 RKMPP                              |
| VP8   | ❌               | `vp8_rkmpp` ✅   | 仅解码；编码器 `vp8_v4l2m2m` 需 V4L2 设备节点 |
| VP9   | ❌               | `vp9_rkmpp` ✅   | 仅解码；RK3588 VPU 无硬编码支持               |
| AV1   | ❌               | `av1_rkmpp` ✅   | 仅解码；RK3588 VPU 无硬编码支持               |

> 无硬编码支持的格式（VP8/VP9/AV1）需要通过软件编码器（`libvpx-vp9`、`libsvtav1` 等）生成测试样本，当前 ffmpeg-rockchip 构建未包含这些软件编码器，因此相关测试标记为 `UNAVAILABLE`。如需测试解码性能，可提供预录样本文件。

## 文档

主 README 只保留项目概览与快速入口，详细说明统一放到文档站源文件中：

- [docs/index.md](docs/index.md)：项目概览与文档导航
- [docs/getting-started.md](docs/getting-started.md)：环境要求、开发容器和首次运行
- [docs/cli.md](docs/cli.md)：CLI 参数、推荐命令组合和 100 kbps 画质档位
- [docs/results.md](docs/results.md)：结果目录结构、字段说明和绘图规则
- [docs/development.md](docs/development.md)：代码结构、测试、CI 和文档维护

本地预览文档站：

```bash
uv sync --dev --group docs
uv run --group docs zensical serve
```

构建静态站点：

```bash
uv run --group docs zensical build
```

## 开发检查

```bash
uv run ruff check .
uv run pytest
```

CI 会执行同一套基础检查，并额外验证文档站构建链路。

## 致谢

- [ffmpeg-rockchip](https://github.com/nyanmisaka/ffmpeg-rockchip) — 为 Rockchip SoC 提供硬件加速编解码的 FFmpeg 分支，本项目依赖其 RKMPP 编解码器。
