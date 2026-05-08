# 开始使用

## 环境要求

- Python 3.11 或更高版本
- `uv` 用于环境和命令管理
- 一套可用的 FFmpeg，可调用 `ffmpeg` / `ffprobe`
- 若要触发 RK3588 硬编硬解，需确保系统中的 Rockchip MPP / V4L2 栈已可用

项目会优先尝试 `/usr/local/ffmpeg-rockchip/bin/ffmpeg` 与 `/usr/local/ffmpeg-rockchip/bin/ffprobe`；若不存在，则回退到系统 PATH 中的同名命令。

## 初始化 Python 环境

```bash
uv sync --dev
```

如果你还需要本地预览文档站，一并安装文档依赖：

```bash
uv sync --dev --group docs
```

## 首次冒烟

```bash
uv run benchmark-vpu --profile quick
```

这个档位适合确认 CLI、FFmpeg 与基础硬件链路是否正常。

## 常见工作流

### 仅做画质测试

```bash
uv run benchmark-vpu --profile quick --quality-only
```

### 画质测试完成后自动出图

```bash
uv run benchmark-vpu --profile quick --quality-only
```

如果只想跑基准不生成图表，可显式传入 `--no-plot-charts`。

### 对既有结果目录补绘图表

```bash
uv run benchmark-vpu --plot-summary results/quality-ladder-20260504
```

### 只跑某一种 codec

```bash
uv run benchmark-vpu --profile quick --h264-only
uv run benchmark-vpu --profile quick --h265-only
uv run benchmark-vpu --profile quick --av1-only
```

## 预览文档站

```bash
uv run --group docs zensical serve
```

默认会启动本地预览服务器。构建静态产物则使用：

```bash
uv run --group docs zensical build
```

!!! note

    根据 ffmpeg-rockchip README，VP9 和 AV1 这里走的是 `vp9_rkmpp`、`av1_rkmpp` 硬解码链路；如果样本编码器或硬解码链路不可用，结果会明确标记为 `UNAVAILABLE`，而不是静默回退到其它 backend。