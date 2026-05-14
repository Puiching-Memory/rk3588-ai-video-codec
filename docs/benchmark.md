# 基准测试

## 运行基准测试

```bash
cd build
./rkvc_bench --quick          # 快速测试 (120 帧)
./rkvc_bench                  # 完整测试 (300 帧)
./rkvc_bench --4k             # 4K 测试
./rkvc_bench --stream         # 包含流式 API 测试
./rkvc_bench --encode-only    # 仅编码测试
./rkvc_bench -o results/run1  # 指定输出目录
```

## 输出格式

结果以 TSV 格式写入 `summary.tsv`：

```
test      size        rate  frames  elapsed_s  fps    realtime  bpp     total_bytes
encode    1920x1080   30    120     0.394      304.7  10.16     0.0056  ...
decode    1920x1080   30    115     0.413      278.6  9.29      12.000  ...
stream_enc 1920x1080  30    118     0.548      215.3  7.18      0.0057  ...
```

## RK3588 实测结果 (1080p)

| 测试       | 帧率     | 实时倍率 | 说明            |
| ---------- | -------- | -------- | --------------- |
| H.265 编码 | ~290 fps | ~9.6x    | RKMPP 硬编码    |
| H.265 解码 | ~270 fps | ~9.0x    | RKMPP 硬解码    |
| 流式编码   | ~215 fps | ~7.2x    | 含 DMA 上传开销 |

## 端到端延迟测试

`latency_test` 模拟摄像头实时采集，经编码 → 解码全链路，测量每帧端到端延迟。

```bash
cd build

# 默认: 1080p@30fps, 4Mbps, 300帧
./example_latency_test

# 低延迟模式 (推荐)
./example_latency_test -l

# 自定义参数
./example_latency_test -s 1280x720 -r 60 -n 600 -b 8000000 -l
```

输出包含逐帧延迟明细和统计摘要（平均、P50、P95、P99）。

### RK3588 延迟实测结果 (1080p, 低延迟模式)

| 指标           | 编码延迟 | 端到端延迟 |
| -------------- | -------- | ---------- |
| 平均           | ~7 ms    | ~69 ms     |
| P50            | ~7 ms    | ~76 ms     |
| P95            | ~8 ms    | ~84 ms     |
| 最大           | ~8 ms    | ~111 ms    |

- **编码延迟**: 采集帧生成到编码包输出，RKMPP 硬编码 ~7ms
- **端到端延迟**: 采集帧生成到解码帧输出，含编码器/解码器硬件流水线
- 帧 0 延迟偏高 (~110ms) 为解码器初始化开销，稳态帧在 56-82ms 交替
