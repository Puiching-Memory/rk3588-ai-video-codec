# 使用指南

## CLI 工具

### rkvc_info

查询硬件能力和系统信息。

```bash
rkvc_info
```

输出示例：
```
rkvc version: 0.1.3
Hardware encoder: available
Hardware decoder: available
Hardware scaler: available
Max resolution: 7680x4320
```

### rkvc_encode

硬件视频编码工具。

**基本用法**：
```bash
rkvc_encode --testsrc -o output.h265 -s 1920x1080 -n 100
```

**常用参数**：
- `--testsrc` — 使用测试图案作为输入
- `-i <file>` — 输入文件（原始 NV12/YUV420P 格式）
- `-o <file>` — 输出文件
- `-s <WxH>` — 分辨率（如 1920x1080）
- `-n <count>` — 编码帧数
- `-b <bitrate>` — 码率（默认 4Mbps）
- `-r <fps>` — 帧率（默认 30）
- `--stdout` — 输出到标准输出（管道模式）

**示例**：
```bash
# 编码原始 YUV 文件
rkvc_encode -i input.yuv -o output.h265 -s 1920x1080 -n 300

# 高码率编码
rkvc_encode --testsrc -o high_quality.h265 -s 3840x2160 -b 20000000 -n 100

# 管道输出
rkvc_encode --testsrc --stdout -s 640x480 -n 30 > output.h265
```

### rkvc_decode

硬件视频解码工具。

**基本用法**：
```bash
rkvc_decode -i input.h265 -o output.nv12
```

**常用参数**：
- `-i <file>` — 输入视频文件
- `-o <file>` — 输出文件（NV12 格式）
- `-s <WxH>` — 输出分辨率（可选，用于缩放）
- `--stdin` — 从标准输入读取（管道模式）
- `--stdout` — 输出到标准输出（管道模式）

**示例**：
```bash
# 基本解码
rkvc_decode -i video.h265 -o decoded.nv12

# 管道模式
cat video.h265 | rkvc_decode --stdin --stdout -s 1920x1080 > decoded.nv12
```

### rkvc_bench

性能基准测试工具。

**基本用法**：
```bash
# 快速测试
rkvc_bench --quick

# 完整测试
rkvc_bench
```

输出编码、解码、流式处理的帧率和延迟统计。

## 示例程序

### 文件编解码

**编码文件**：
```bash
./examples/bin/example_encode_file input.yuv output.h265 1920 1080
```

**解码文件**：
```bash
./examples/bin/example_decode_file input.h265 output.nv12
```

### 流式处理

**流式编码**：
```bash
./examples/bin/example_stream_encode output.h265 1920 1080 300
```

**流式解码**：
```bash
./examples/bin/example_stream_decode input.h265
```

### 转码

**基本转码**：
```bash
./examples/bin/example_transcode input.h265 output.h265
```

**降分辨率转码**：
```bash
./examples/bin/example_stream_transcode -i input.h265 -o output.h265 -s 1280x720
```

### 双设备流式传输

模拟两台设备之间的实时流式传输：

**UDP 模式**：
```bash
# 发送端（设备 A）
./examples/bin/example_stream_device_pair -i input.h265 -m udp -r send -a 192.168.1.100 -p 5000

# 接收端（设备 B）
./examples/bin/example_stream_device_pair -m udp -r recv -p 5000
```

**RTP 模式**：
```bash
# 发送端
./examples/bin/example_stream_device_pair -i input.h265 -m rtp -r send -a 192.168.1.100 -p 5004

# 接收端
./examples/bin/example_stream_device_pair -m rtp -r recv -p 5004
```

### 延迟测试

测试端到端编解码延迟：

```bash
# 低延迟模式，1080p@30fps
./examples/bin/example_latency_test -l

# 自定义分辨率和帧率
./examples/bin/example_latency_test -s 1280x720 -r 60 -l
```

输出逐帧延迟统计（P50/P95/P99）。

### 质量测试

测试编解码质量（PSNR）：

```bash
# 基本测试
./examples/bin/example_psnr_test -i input.h265

# 详细输出（逐帧 PSNR）
./examples/bin/example_psnr_test -i input.h265 -v -n 100
```

输出 Y/U/V 通道 PSNR 及加权平均值。

## 常见问题

### 权限错误

如遇权限错误，请联系技术支持配置设备权限。

### 库依赖缺失

如遇到库依赖问题，请联系技术支持获取必要的运行时库。

### 性能优化

- 使用低延迟模式（`-l` 参数）可减少编码延迟
- 适当调整码率（`-b` 参数）平衡质量和性能
- 对于实时应用，建议使用流式 API 而非文件 API

## 环境变量

- `LD_LIBRARY_PATH` — 如果使用可移植包，需设置为 `$PWD/lib`
- `PKG_CONFIG_PATH` — 如果使用可移植包，需设置为 `$PWD/share/pkgconfig`

**示例**：
```bash
export LD_LIBRARY_PATH=$PWD/lib:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=$PWD/share/pkgconfig:$PKG_CONFIG_PATH
```
