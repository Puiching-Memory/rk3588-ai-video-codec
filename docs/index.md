# rk3588-ai-video-codec

面向 RK3588 的 FFmpeg VPU 基准 CLI，聚焦硬件编解码吞吐、画质还原度以及结果可视化。

<div class="hero-grid" markdown>
<div class="hero-card" markdown>

## 覆盖内容

- H.264 / H.265 的硬编码、硬解码与往返链路
- VP8、VP9、AV1 的可用性探测与扩展质量测试
- 统一生成 `summary.tsv`、`summary.md`、`logs/`、`artifacts/`、`plots/`
- 基于结果目录补绘码率、画质、吞吐和延迟图表

</div>
<div class="hero-card" markdown>

## 常用命令

```bash
uv run benchmark-vpu --profile quick
uv run benchmark-vpu --profile quick --quality-ladder
uv run benchmark-vpu --plot-summary results/quality-ladder-20260504
```

</div>
</div>

## 适用场景

- 验证 FFmpeg Rockchip 栈在目标机器上是否已经打通
- 比较不同 codec 在同一台 RK3588 设备上的吞吐和时延表现
- 采集 100 kbps 单档位质量数据，并输出左右预览图观察压缩效果
- 对既有测试结果补出标准图表，便于归档、分享和汇报

## 文档导航

- [开始使用](getting-started.md)：环境要求、初始化和首次运行
- [CLI 参考](cli.md)：参数说明与推荐组合
- [结果与图表](results.md)：输出目录、关键字段和绘图入口
- [开发说明](development.md)：代码结构、测试和文档维护方式
- [AI 视频编解码时间线](ai-video-codec-timeline.md)：神经网络视频压缩领域的关键里程碑与发展脉络

## 当前边界

- 当前实现统一走 FFmpeg CLI，不再包含 GStreamer backend
- VP9 和 AV1 是否能完整跑通，取决于本机 FFmpeg 是否带有对应样本编码器以及 `vp9_rkmpp` / `av1_rkmpp` 解码链路
- `--quality-ladder` 在未指定 codec-only 时会自动带上 VP8、VP9、AV1 的精简扩展质量测试

!!! tip

    如果你已经跑过一次基准测试，最快的验证方式通常是直接对结果目录执行 `uv run benchmark-vpu --plot-summary <result_dir>`。