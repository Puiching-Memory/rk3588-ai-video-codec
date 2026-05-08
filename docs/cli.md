# CLI 参考

## 基本形式

```bash
uv run benchmark-vpu [options]
```

兼容入口仍然保留：

```bash
bash scripts/benchmark_vpu.sh [options]
```

## 运行档位

- `--profile {quick,full}`：选择测试档位，默认 `full`
- `--out-dir PATH`：指定结果输出目录，默认写入 `results/<timestamp>`
- `--strict`：只要存在 `FAIL` 或 `UNAVAILABLE`，进程就返回非 0 退出码

## 选择 codec 范围

- `--h264-only`：仅运行 H.264 相关测试
- `--h265-only`：仅运行 H.265 相关测试
- `--av1-only`：仅运行 AV1 相关测试
- `--skip-4k`：跳过 H.264 / H.265 的 4K 档位

这三个 `only` 选项互斥。

## 画质与扩展测试

- `--quality-ladder`：在吞吐测试之外，追加 H.264 / H.265 的质量阶梯测试
- `--quality-only`：只跑画质还原度测试，不执行常规吞吐和 AV1 探测
- `--quality-extra-codecs`：追加 VP8、VP9、AV1 的扩展质量测试

默认质量阶梯会覆盖以下目标码率：

- 360p -> 500 kbps
- 480p -> 1000 kbps
- 720p -> 1500 kbps
- 1080p -> 2500 kbps
- 1080p -> 3500 kbps

## 绘图参数

- `--plot-summary PATH`：读取结果目录或其中的 `summary.tsv`，生成图表后退出
- `--plot-out-dir PATH`：自定义图表输出目录，默认是结果目录下的 `plots/`
- `--plot-title TEXT`：覆盖图表标题
- `--plot-charts`：在基准测试结束后自动出图

`--plot-summary` 与 `--plot-charts` 不能同时使用。

## 推荐命令组合

### 快速冒烟

```bash
uv run benchmark-vpu --profile quick
```

### 正式采样并追加质量阶梯

```bash
uv run benchmark-vpu --profile full --quality-ladder
```

### 只做扩展质量测试并自动绘图

```bash
uv run benchmark-vpu --profile quick --quality-only --quality-extra-codecs --plot-charts
```

### 严格模式，适合 CI 或批处理

```bash
uv run benchmark-vpu --profile quick --strict
```