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
- `zensical.toml`：Zensical 文档站配置
- `.python-version`：固定本地 Python 主版本
- `docs/`：文档站 Markdown 页面和样式
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

CI 通过 GitHub Actions 执行同一套基础检查，工作流位于 `.github/workflows/ci.yml`，包含：

- `uv run ruff check .`
- `uv run pytest`
- `uv run --group docs zensical build`

说明：CI 不会在通用 GitHub runner 上执行 RK3588 硬件基准，只校验静态检查、单元测试和文档站构建。

### 文档站

仓库已添加基于 Zensical 的静态文档站，配置位于 `zensical.toml`，内容位于 `docs/`。

安装文档依赖并启动本地预览：

```bash
uv sync --dev --group docs
uv run --group docs zensical serve
```

构建静态站点：

```bash
uv run --group docs zensical build
```

默认构建产物输出到根目录下的 `site/`。

### 覆盖范围

当前项目已移除 GStreamer 和混合 backend，所有测试步骤统一通过 FFmpeg CLI 执行。

- H.264 硬编码：`h264_rkmpp`
- H.264 硬解码：`h264_rkmpp`
- H.264 编解码往返测试：`h264_rkmpp -> h264_rkmpp`
- H.265 硬编码：`hevc_rkmpp`
- H.265 硬解码：`hevc_rkmpp`
- H.265 编解码往返测试：`hevc_rkmpp -> hevc_rkmpp`
- MJPEG 硬编码：`mjpeg_rkmpp`
- MJPEG 硬解码：`mjpeg_rkmpp`
- VP8 FFmpeg 样本编码：优先尝试 `vp8_rkmpp`，否则回退到 `vp8_v4l2m2m`
- VP8 硬解码：`vp8_rkmpp`
- VP9 硬解码探测：FFmpeg 样本生成 + `vp9_rkmpp`
- AV1 硬编码可用性检查：`av1_rkmpp`
- AV1 硬解码探测：FFmpeg 样本生成 + `av1_rkmpp`

### 执行方式

完整档位：

```bash
uv run benchmark-vpu --profile full
```

快速冒烟：

```bash
uv run benchmark-vpu --profile quick
```

说明：默认吞吐测试会额外包含 MJPEG、VP8、VP9 的探测；如果使用 `--h264-only`、`--h265-only` 或 `--av1-only`，则不会附带这些额外 codec。

说明：VP9 和 AV1 当前是否能真正跑通，取决于这台机器上的 FFmpeg 是否同时包含可用于样本生成的编码器；如果缺失，结果会明确标记为 `UNAVAILABLE`，而不会回退到 GStreamer。

`--quality-extra-codecs` 会追加 MJPEG、VP8、VP9、AV1 的扩展画质测试。为控制运行时长，这组扩展画质默认采用精简档位：MJPEG/VP8/VP9 使用 360p、480p、720p 三档，AV1 使用 360p、720p 两档。

画质还原度测试：

```bash
uv run benchmark-vpu --profile quick --quality-only
```

追加 MJPEG/VP8/VP9/AV1 扩展画质测试：

```bash
uv run benchmark-vpu --profile quick --quality-only --quality-extra-codecs
```

对已有结果自动绘图：

```bash
uv run benchmark-vpu --plot-summary results/quality-ladder-20260504
```

仅运行 H.264：

```bash
uv run benchmark-vpu --profile quick --h264-only
```

指定输出目录：

```bash
uv run benchmark-vpu --profile full --out-dir ./results/manual-run
```

吞吐和画质一起跑：

```bash
uv run benchmark-vpu --profile full --quality-ladder
```

测试结束后自动出图：

```bash
uv run benchmark-vpu --profile quick --quality-only --plot-charts
```

### 输出内容

- `system.txt`：硬件、内核、ffmpeg、V4L2 快照
- `summary.tsv`：机器可读汇总，包含显式 `backend` 列，当前固定为 `ffmpeg`
- `summary.md`：人类可读汇总，包含显式 Backend 列，当前固定为 `ffmpeg`
- `logs/`：每一步原始日志
- `artifacts/`：生成的测试码流样本
- `plots/`：自动生成的码率-画质-性能-延迟图表

### 画质还原度档位

启用 `--quality-ladder` 或 `--quality-only` 后，会追加以下 H.264/H.265 档位，并在汇总的 `note` 字段写入 `psnr_avg`、`ssim_all`、`ssim_db` 和实际 `avg_kbps`：

- 360p -> 500 kbps
- 480p -> 1000 kbps
- 720p -> 1500 kbps
- 1080p -> 2500 kbps
- 1080p -> 3500 kbps

说明：480p 档位默认使用 848x480，以提高 Rockchip 硬编码兼容性。

扩展画质测试的实现方式：

- MJPEG：`mjpeg_rkmpp` 硬编码 + `mjpeg_rkmpp` 硬解码
- VP8：FFmpeg 样本编码器 `vp8_rkmpp` / `vp8_v4l2m2m` + `vp8_rkmpp` 硬解码
- VP9：FFmpeg 样本编码器（如 `libvpx-vp9`）+ `vp9_rkmpp` 硬解码
- AV1：FFmpeg 样本编码器（如 `av1_rkmpp`、`libsvtav1`、`librav1e`、`libaom-av1`）+ `av1_rkmpp` 硬解码

### 自动绘图

`--plot-summary` 支持直接读取某次结果目录或其中的 `summary.tsv`，自动输出以下 PNG：

- `rd_performance_latency_dashboard.png`：总览看板
- `bitrate_vs_psnr.png`：码率-PSNR 曲线
- `bitrate_vs_ssim.png`：码率-SSIM 曲线
- `bitrate_vs_fps.png`：码率-吞吐曲线
- `bitrate_vs_latency_ms.png`：码率-单帧延迟曲线

默认输出到对应结果目录下的 `plots/`，也可以通过 `--plot-out-dir` 指定其它目录。

图表选择规则：

- PSNR/SSIM 图只使用 `quality` 结果
- FPS/Latency 图会合并 `quality` 与普通吞吐结果，因此 MJPEG、VP8、VP9 即使只做了部分质量档位，也会进入同一套性能/延迟图

### 说明

- 项目最低 Python 版本现为 3.11。
- `quick` 适合脚本冒烟和环境自检，`full` 适合正式采样。
- `--skip-4k` 会同时跳过 H.264 和 H.265 的 4K 档位。
- 若当前系统栈没有打通 AV1 硬解码，脚本会在汇总中标记为 `UNAVAILABLE`，并保留定位日志。
- `scripts/benchmark_vpu.sh` 只是兼容入口；新的主入口是 `uv run benchmark-vpu`。
