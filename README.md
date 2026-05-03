# rk3588-ai-video-codec
研究在RK3588上运行神经网络视频编解码

## 开发容器

仓库已添加 `.devcontainer`，可在 Windows 的 WSL2 + Docker Desktop 环境中直接使用 VS Code Dev Containers 打开。

### 预装内容

- CUDA 12.8（基于 `nvidia/cuda:12.8.1-cudnn-devel-ubuntu22.04`）
- Python 3
- `uv`
- 常用构建工具（`build-essential`、`git`、`curl`）

### 使用前提

- Windows 已启用 WSL2
- Docker Desktop 已开启 WSL 集成
- 主机已具备可用的 NVIDIA GPU 容器支持

### 使用方式

1. 在 VS Code 中安装 Dev Containers 扩展。
2. 用 VS Code 打开本仓库。
3. 执行 `Dev Containers: Reopen in Container`。
4. 容器启动后可用 `uv --version` 和 `python3 --version` 验证环境。

## uv Python 项目

仓库现已重构为基于 `uv` 管理的 Python 应用项目，核心入口是 `benchmark-vpu` CLI，源码位于 `src/rk3588_ai_video_codec/`。现有 `scripts/benchmark_vpu.sh` 保留为兼容包装层，内部会转发到 `uv run benchmark-vpu`。

### 项目结构

- `pyproject.toml`：`uv` 项目元数据和 CLI 入口
- `.python-version`：固定本地 Python 主版本
- `src/rk3588_ai_video_codec/`：基准逻辑和命令行入口
- `scripts/benchmark_vpu.sh`：兼容包装脚本
- `results/`：默认输出目录

### 初始化环境

首次拉起本地环境：

```bash
uv sync --dev
```

直接运行 CLI：

```bash
uv run benchmark-vpu --profile quick
```

兼容旧入口：

```bash
bash scripts/benchmark_vpu.sh --profile quick
```

开发检查：

```bash
uv run ruff check .
uv run pytest
```

### 覆盖范围

- H.264 硬编码：`h264_rkmpp`
- H.264 硬解码：`h264_rkmpp`
- H.264 流式编解码流水线：`mpph264enc -> mppvideodec`
- H.265 硬编码：`hevc_rkmpp`
- H.265 硬解码：`hevc_rkmpp`
- H.265 流式编解码流水线：`mpph265enc -> mppvideodec`
- AV1 硬编码可用性检查
- AV1 硬解码探测：先走软件解码基线，再探测 `mppvideodec`

### 执行方式

完整档位：

```bash
uv run benchmark-vpu --profile full
```

快速冒烟：

```bash
uv run benchmark-vpu --profile quick
```

仅运行 H.264：

```bash
uv run benchmark-vpu --profile quick --h264-only
```

指定输出目录：

```bash
uv run benchmark-vpu --profile full --out-dir ./results/manual-run
```

### 输出内容

- `system.txt`：硬件、内核、ffmpeg、V4L2 快照
- `summary.tsv`：机器可读汇总
- `summary.md`：人类可读汇总
- `logs/`：每一步原始日志
- `artifacts/`：生成的测试码流样本

### 说明

- 项目最低 Python 版本现为 3.11。
- `quick` 适合脚本冒烟和环境自检，`full` 适合正式采样。
- `--skip-4k` 会同时跳过 H.264 和 H.265 的 4K 档位。
- 若当前系统栈没有打通 AV1 硬解码，脚本会在汇总中标记为 `UNAVAILABLE`，并保留定位日志。
- `scripts/benchmark_vpu.sh` 只是兼容入口；新的主入口是 `uv run benchmark-vpu`。
