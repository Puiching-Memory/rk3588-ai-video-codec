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
