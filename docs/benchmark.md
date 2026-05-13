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

| 测试 | 帧率 | 实时倍率 | 说明 |
|-----|------|---------|------|
| H.265 编码 | ~290 fps | ~9.6x | RKMPP 硬编码 |
| H.265 解码 | ~270 fps | ~9.0x | RKMPP 硬解码 |
| 流式编码 | ~215 fps | ~7.2x | 含 DMA 上传开销 |
