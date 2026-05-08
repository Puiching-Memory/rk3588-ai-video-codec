# 结果与图表

## 输出目录结构

每次运行默认会在 `results/` 下创建一个带时间戳或自定义名称的结果目录，常见内容如下：

- `system.txt`：硬件、内核、FFmpeg、V4L2 等环境快照
- `summary.tsv`：机器可读汇总，适合脚本解析和绘图输入
- `summary.md`：面向人阅读的汇总报告
- `logs/`：每一步原始日志，适合排查失败原因
- `artifacts/`：生成的测试码流样本
- `plots/`：自动生成的 PNG 图表

## `summary.tsv` 关键字段

- `status`：`PASS`、`FAIL` 或 `UNAVAILABLE`
- `codec`：H264、H265、VP8、VP9、AV1 等
- `type`：例如 `encode`、`decode`、`quality`
- `backend`：当前实现固定为 `ffmpeg`
- `case`：用例名，如 `360p30_500kbps`
- `size` / `rate` / `frames`：尺寸、帧率、帧数
- `elapsed_s` / `fps` / `realtime` / `cpu`：性能指标
- `note`：补充指标，常见键包括 `target`、`avg_kbps`、`avg_mbps`、`psnr_avg`、`ssim_all`、`ssim_db`
- `artifact`：对应输出文件路径

`src/rk3588_ai_video_codec/plotting.py` 读取的就是这些字段，其中 `note` 内的键值对会被拆解成图表数据。

## 自动生成的图表

执行 `--plot-charts` 或 `--plot-summary` 后，默认会生成以下 PNG：

- `rd_performance_latency_dashboard.png`
- `bitrate_vs_psnr.png`
- `bitrate_vs_ssim.png`
- `bitrate_vs_fps.png`
- `bitrate_vs_latency_ms.png`

图表选择规则如下：

- PSNR / SSIM 图只读取 `quality` 结果
- FPS / 延迟图优先合并有码率数据的运行结果
- 若不存在常规吞吐点，绘图逻辑会退回到质量数据

## 补绘已有结果

```bash
uv run benchmark-vpu --plot-summary results/quality-ladder-20260504
```

也可以直接指向单个 `summary.tsv`：

```bash
uv run benchmark-vpu --plot-summary results/quality-ladder-20260504/summary.tsv
```

## 常见排查入口

- 某个 codec 失败时，优先看结果目录下的 `logs/`
- 图表没有输出时，先检查 `summary.tsv` 是否包含 `PASS` 且带有码率信息的行
- 画质指标缺失时，检查 `note` 是否包含 `psnr_avg` 与 `ssim_all`