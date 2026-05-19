# 示例程序指南

本文档详细介绍各示例程序的使用方法和应用场景。所有示例程序位于 `examples/` 目录，包含源码和编译好的二进制文件。

## 目录

- [文件编解码](#文件编解码)
- [流式处理](#流式处理)
- [转码](#转码)
- [网络传输](#网络传输)
- [性能测试](#性能测试)
- [质量测试](#质量测试)

---

## 文件编解码

### encode_file - 文件编码

将原始 YUV/NV12 视频文件编码为压缩格式。

**用法**：
```bash
./examples/bin/example_encode_file <input.yuv> <output.h265> <width> <height>
```

**示例**：
```bash
# 编码 1080p YUV 文件
./examples/bin/example_encode_file input_1920x1080.yuv output.h265 1920 1080

# 编码 4K YUV 文件
./examples/bin/example_encode_file input_3840x2160.yuv output.h265 3840 2160
```

**应用场景**：
- 离线视频编码
- 批量视频处理
- 视频格式转换

**源码位置**：`examples/src/encode_file.c`

---

### decode_file - 文件解码

将压缩视频文件解码为原始 NV12 格式。

**用法**：
```bash
./examples/bin/example_decode_file <input.h265> <output.nv12>
```

**示例**：
```bash
# 解码视频文件
./examples/bin/example_decode_file video.h265 decoded.nv12

# 解码后用 ffplay 播放
./examples/bin/example_decode_file video.h265 decoded.nv12
ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 decoded.nv12
```

**应用场景**：
- 视频预览
- 帧提取
- 视频分析

**源码位置**：`examples/src/decode_file.c`

---

## 流式处理

### stream_encode - 流式编码

演示流式编码 API，逐帧推送数据到编码器。

**用法**：
```bash
./examples/bin/example_stream_encode <output.h265> <width> <height> <frame_count>
```

**示例**：
```bash
# 编码 300 帧 1080p 测试图案
./examples/bin/example_stream_encode output.h265 1920 1080 300

# 编码 100 帧 720p
./examples/bin/example_stream_encode output.h265 1280 720 100
```

**应用场景**：
- 实时视频采集编码
- 摄像头流编码
- 屏幕录制

**关键 API**：
- `rkvc_stream_create()` - 创建编码流
- `rkvc_stream_push()` - 推送原始帧
- `rkvc_stream_pull()` - 拉取编码帧

**源码位置**：`examples/src/stream_encode.c`

---

### stream_decode - 流式解码

演示流式解码 API，逐帧拉取解码数据。

**用法**：
```bash
./examples/bin/example_stream_decode <input.h265>
```

**示例**：
```bash
# 流式解码并统计帧数
./examples/bin/example_stream_decode video.h265
```

**应用场景**：
- 实时视频播放
- 视频流接收解码
- 帧级处理

**关键 API**：
- `rkvc_stream_create()` - 创建解码流
- `rkvc_stream_push()` - 推送编码数据
- `rkvc_stream_pull()` - 拉取解码帧

**源码位置**：`examples/src/stream_decode.c`

---

## 转码

### transcode - 基本转码

演示解码 → 编码转码流程，支持分辨率转换。

**用法**：
```bash
./examples/bin/example_transcode <input.h265> <output.h265>
```

**示例**：
```bash
# 基本转码（保持分辨率）
./examples/bin/example_transcode input.h265 output.h265
```

**应用场景**：
- 码率转换
- 视频重编码
- 格式标准化

**源码位置**：`examples/src/transcode.c`

---

### stream_transcode - 流式转码

演示流式转码，支持自定义输出分辨率和码率。

**用法**：
```bash
./examples/bin/example_stream_transcode -i <input.h265> -o <output.h265> [-s WxH] [-b bitrate]
```

**示例**：
```bash
# 降分辨率转码（1080p → 720p）
./examples/bin/example_stream_transcode -i input_1080p.h265 -o output_720p.h265 -s 1280x720

# 降码率转码
./examples/bin/example_stream_transcode -i input.h265 -o output.h265 -b 2000000

# 4K → 1080p 转码
./examples/bin/example_stream_transcode -i input_4k.h265 -o output_1080p.h265 -s 1920x1080
```

**应用场景**：
- 多码率转码（自适应流）
- 分辨率适配
- 带宽优化

**关键特性**：
- 自动硬件缩放
- 零拷贝帧传递

**源码位置**：`examples/src/stream_transcode.c`

---

## 网络传输

### stream_device_pair - 双设备流式传输

演示两台 RK3588 设备之间的实时视频流传输，支持 UDP 和 RTP 两种协议。

**用法**：
```bash
./examples/bin/example_stream_device_pair -i <input.h265> -m <mode> -r <role> [-a <addr>] [-p <port>]
```

**参数说明**：
- `-i` — 输入视频文件（发送端必需）
- `-m` — 传输模式：`udp` 或 `rtp`
- `-r` — 角色：`send`（发送）、`recv`（接收）、`both`（本地回环测试）
- `-a` — 目标 IP 地址（发送端必需）
- `-p` — UDP 端口（默认 5000）

**示例 1：UDP 模式双设备传输**

设备 A（发送端）：
```bash
./examples/bin/example_stream_device_pair \
  -i video.h265 \
  -m udp \
  -r send \
  -a 192.168.1.100 \
  -p 5000
```

设备 B（接收端）：
```bash
./examples/bin/example_stream_device_pair \
  -m udp \
  -r recv \
  -p 5000
```

**示例 2：RTP 模式双设备传输**

设备 A（发送端）：
```bash
./examples/bin/example_stream_device_pair \
  -i video.h265 \
  -m rtp \
  -r send \
  -a 192.168.1.100 \
  -p 5004
```

设备 B（接收端）：
```bash
./examples/bin/example_stream_device_pair \
  -m rtp \
  -r recv \
  -p 5004
```

**示例 3：本地回环测试**

```bash
# 单机测试（发送和接收在同一设备）
./examples/bin/example_stream_device_pair \
  -i video.h265 \
  -m udp \
  -r both \
  -a 127.0.0.1 \
  -p 5000
```

**应用场景**：
- 视频监控系统
- 远程视频传输
- 流媒体推送
- 设备间视频通信

**协议对比**：

| 特性     | UDP 模式         | RTP 模式     |
| -------- | ---------------- | ------------ |
| 封装     | 自定义 16B 头    | 标准 RTP 头  |
| 分片     | 自动分片（大帧） | NAL 单元分片 |
| 兼容性   | 专用协议         | 标准协议     |
| 适用场景 | 专用系统         | 标准流媒体   |

**源码位置**：`examples/src/stream_device_pair.c`

---

## 性能测试

### latency_test - 延迟测试

测试端到端编解码延迟（编码 → 解码全链路）。

**用法**：
```bash
./examples/bin/example_latency_test [-s WxH] [-r fps] [-l] [-n count]
```

**参数说明**：
- `-s` — 分辨率（默认 1920x1080）
- `-r` — 帧率（默认 30）
- `-l` — 启用低延迟模式
- `-n` — 测试帧数（默认 300）

**示例**：
```bash
# 1080p@30fps 低延迟测试
./examples/bin/example_latency_test -l

# 720p@60fps 测试
./examples/bin/example_latency_test -s 1280x720 -r 60 -l

# 4K@30fps 测试
./examples/bin/example_latency_test -s 3840x2160 -r 30 -l -n 100
```

**输出示例**：
```
Frame 0: 12.34 ms
Frame 1: 11.89 ms
Frame 2: 12.01 ms
...
--- Latency Statistics ---
P50: 12.00 ms
P95: 13.50 ms
P99: 14.20 ms
Average: 12.15 ms
```

**应用场景**：
- 实时性能评估
- 系统调优
- 延迟敏感应用验证

**源码位置**：`examples/src/latency_test.c`

---

## 质量测试

### psnr_test - PSNR 质量测试

测试编解码质量，计算原始帧与重建帧的 PSNR（峰值信噪比）。

**用法**：
```bash
./examples/bin/example_psnr_test -i <input.h265> [-v] [-n count]
```

**参数说明**：
- `-i` — 输入视频文件
- `-v` — 详细模式（输出逐帧 PSNR）
- `-n` — 测试帧数（默认全部）

**示例**：
```bash
# 基本质量测试
./examples/bin/example_psnr_test -i video.h265

# 详细输出前 100 帧
./examples/bin/example_psnr_test -i video.h265 -v -n 100
```

**输出示例**：
```
Frame 0: Y=42.35 U=44.21 V=43.89 Avg=42.81 dB
Frame 1: Y=41.98 U=44.05 V=43.67 Avg=42.56 dB
...
--- PSNR Statistics ---
Average Y: 42.15 dB
Average U: 44.10 dB
Average V: 43.75 dB
Weighted Average: 42.67 dB
Minimum Frame: 39.82 dB (frame 87)
```

**PSNR 参考值**：
- \> 40 dB — 优秀质量
- 35-40 dB — 良好质量
- 30-35 dB — 可接受质量
- < 30 dB — 质量较差

**应用场景**：
- 编码质量评估
- 码率优化
- 质量基准测试

**源码位置**：`examples/src/psnr_test.c`

---

## 编译示例程序

所有示例程序源码位于 `examples/src/`，可以作为二次开发的参考。

**编译单个示例**：
```bash
export PKG_CONFIG_PATH=$PWD/share/pkgconfig:$PKG_CONFIG_PATH
export LD_LIBRARY_PATH=$PWD/lib:$LD_LIBRARY_PATH

gcc -o my_encoder examples/src/encode_file.c $(pkg-config --cflags --libs rkvc)
```

**运行自编译程序**：
```bash
./my_encoder input.yuv output.h265 1920 1080
```

详见 [二次开发指南](DEVELOPMENT.md)。
