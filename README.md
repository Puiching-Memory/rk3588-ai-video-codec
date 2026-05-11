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

追加统一画质测试：

```bash
uv run benchmark-vpu --profile quick --quality-ladder
```

对已有结果目录补绘图表：

```bash
uv run benchmark-vpu --plot-summary results/<run-dir>
```

## 文档

主 README 只保留项目概览与快速入口，详细说明统一放到文档站源文件中：

- [docs/index.md](docs/index.md)：项目概览与文档导航
- [docs/getting-started.md](docs/getting-started.md)：环境要求、开发容器和首次运行
- [docs/cli.md](docs/cli.md)：CLI 参数、推荐命令组合和质量档位
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
