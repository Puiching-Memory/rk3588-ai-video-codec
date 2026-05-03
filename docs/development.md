# 开发说明

## 代码结构

- `src/rk3588_ai_video_codec/benchmark.py`：测试编排、FFmpeg 调用、结果汇总
- `src/rk3588_ai_video_codec/cli.py`：命令行参数和主入口
- `src/rk3588_ai_video_codec/plotting.py`：读取 `summary.tsv` 并渲染图表
- `tests/test_benchmark.py`：基础计算与 CLI 相关测试
- `tests/test_plotting.py`：汇总解析与绘图烟雾测试

## 开发检查

```bash
uv run ruff check .
uv run pytest
```

## CI

仓库的 GitHub Actions 工作流位于 `.github/workflows/ci.yml`，会在 push 和 pull request 上执行：

- `uv sync --all-groups --locked`
- `uv run ruff check .`
- `uv run pytest`
- `uv run --group docs zensical build`

由于 GitHub 托管 runner 不具备 RK3588 的 Rockchip 硬件环境，CI 不执行实际的 VPU 基准，仅验证 Python 代码质量、单元测试和文档构建链路。

## 文档站维护

文档站根配置位于 `zensical.toml`，源文件位于 `docs/`。

本地预览：

```bash
uv run --group docs zensical serve
```

构建静态产物：

```bash
uv run --group docs zensical build
```

站点默认输出到仓库根目录下的 `site/`，该目录已加入 `.gitignore`。

## 维护建议

- CLI 参数或输出结构变更时，同步更新 `README.md` 与这里的文档页面
- 如果新增结果字段，优先补充 `tests/test_plotting.py` 中的覆盖样例
- 如果调整默认工作流，优先更新 [开始使用](getting-started.md) 与 [CLI 参考](cli.md)