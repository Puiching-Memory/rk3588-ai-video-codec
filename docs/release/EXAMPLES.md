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

生成测试 NV12 帧并编码为 H.265 文件。

**用法**：
```bash
./examples/bin/example_encode_file -o <output.h265> [-s WxH] [-r fps] [-n count] [-b bitrate]
```

**示例**：
```bash
# 编码 300 帧 1080p 测试图案
./examples/bin/example_encode_file -o output.h265 -s 1920x1080 -n 300

# 编码 100 帧 4K 测试图案
./examples/bin/example_encode_file -o output_4k.h265 -s 3840x2160 -n 100 -b 20000000
```

**应用场景**：
- 离线视频编码
- 批量视频处理
- 视频格式转换

**源码位置**：`examples/encode_file.c`

---

### decode_file - 文件解码

将压缩视频文件解码为原始 NV12 格式。

**用法**：
```bash
./examples/bin/example_decode_file <input.h265> [-o output.nv12]
```

**示例**：
```bash
# 解码视频文件
./examples/bin/example_decode_file video.h265 -o decoded.nv12

# 解码后用 ffplay 播放
./examples/bin/example_decode_file video.h265 -o decoded.nv12
ffplay -f rawvideo -pixel_format nv12 -video_size 1920x1080 decoded.nv12
```

**应用场景**：
- 视频预览
- 帧提取
- 视频分析

**源码位置**：`examples/decode_file.c`

---

### decode_formats - 解码输出格式验证

演示 `rkvc_decoder_config.output_format` 的实际生效行为。编码一段测试 H.265 流，再分别以 NV12 / YUV420P / NV16 / P010 作为 `output_format` 解码，逐帧打印实际格式并与配置比对。

**用法**：
```bash
./examples/bin/example_decode_formats
```

**示例输出**：
```
=== 以 NV12 格式解码 ===
  帧 0: format=NV12 (期望 NV12) ✓
  ...
=== 以 YUV420P 格式解码 ===
  帧 0: format=YUV420P (期望 YUV420P) ✓
  ...
=== 总结 ===
所有格式输出正确: 是 ✓
```

**说明**：
- NV12 走硬件直出，零额外开销。
- YUV420P / NV16 / P010 在硬件帧池不支持时由 `libswscale` 软件像素格式转换补齐，交付帧格式保证等于 `output_format` 配置值。
- 本示例对应 v0.1.6 修复的「输出格式静默失效」回归问题。

**源码位置**：`examples/decode_formats.c`

---

## 流式处理

### stream_encode - 流式编码

演示流式编码 API，逐帧推送测试帧并从内存中拉取编码包。

**用法**：
```bash
./examples/bin/example_stream_encode [-s WxH] [-r fps] [-n count] [-b bitrate]
```

**示例**：
```bash
# 编码 300 帧 1080p 测试图案
./examples/bin/example_stream_encode -s 1920x1080 -n 300

# 编码 100 帧 720p
./examples/bin/example_stream_encode -s 1280x720 -n 100
```

**应用场景**：
- 实时视频采集编码
- 摄像头流编码
- 屏幕录制

**关键 API**：
- `rkvc_stream_open()` - 创建编码流
- `rkvc_stream_push()` - 推送原始帧
- `rkvc_stream_pull()` - 拉取编码包
- `rkvc_stream_close()` - 关闭编码流

**源码位置**：`examples/stream_encode.c`

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
- `rkvc_stream_open()` - 创建解码流
- `rkvc_stream_push()` - 推送编码数据
- `rkvc_stream_pull()` - 拉取解码帧
- `rkvc_stream_close()` - 关闭解码流

**源码位置**：`examples/stream_decode.c`

---

## 转码

### transcode - 基本转码

演示解码 → 编码转码流程，支持分辨率转换。

**用法**：
```bash
./examples/bin/example_transcode -i <input.h265> -o <output.h265> [-s WxH] [-b bitrate]
```

**示例**：
```bash
# 基本转码（保持分辨率）
./examples/bin/example_transcode -i input.h265 -o output.h265
```

**应用场景**：
- 码率转换
- 视频重编码
- 格式标准化

**源码位置**：`examples/transcode.c`

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

**源码位置**：`examples/stream_transcode.c`

---

## 网络传输

### stream_device_pair - 双设备流式传输

演示两台 RK3588 设备之间的实时视频流传输，支持 UDP 和 RTP 两种协议。

**用法**：
```bash
./examples/bin/example_stream_device_pair -i <input.h265> -c <mode> -r <role> [--dst-ip <addr>] [--dst-port <port>]
```

**参数说明**：
- `-i` — 输入视频文件（发送端必需）
- `-c` — 传输模式：`udp` 或 `rtp`
- `-r` — 角色：`send`（发送）、`recv`（接收）、`both`（本地回环测试）
- `--dst-ip` — 目标 IP 地址（发送端必需，默认 `127.0.0.1`）
- `--dst-port` — 目标端口（默认 `9000`）
- `--bind-port` — 接收端绑定端口（默认 `9000`）

**快速本机端到端测试**

```bash
./network-e2e-test.sh
./network-e2e-test.sh --mode rtp --frames 30
```

**示例 1：UDP 模式双设备传输**

设备 A（发送端）：
```bash
./examples/bin/example_stream_device_pair \
  -i video.h265 \
  -c udp \
  -r send \
  --dst-ip 192.168.1.100 \
  --dst-port 5000
```

设备 B（接收端）：
```bash
./examples/bin/example_stream_device_pair \
  -c udp \
  -r recv \
  --bind-port 5000
```

**示例 2：RTP 模式双设备传输**

设备 A（发送端）：
```bash
./examples/bin/example_stream_device_pair \
  -i video.h265 \
  -c rtp \
  -r send \
  --dst-ip 192.168.1.100 \
  --dst-port 5004
```

设备 B（接收端）：
```bash
./examples/bin/example_stream_device_pair \
  -c rtp \
  -r recv \
  --bind-port 5004
```

**示例 3：本地回环测试**

```bash
# 单机测试（发送和接收在同一设备）
./examples/bin/example_stream_device_pair \
  -i video.h265 \
  -c udp \
  -r both \
  --dst-ip 127.0.0.1 \
  --dst-port 5000 \
  --bind-port 5000
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

**源码位置**：`examples/stream_device_pair.c`

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

**源码位置**：`examples/latency_test.c`

---

## 质量测试

### psnr_test - PSNR 质量测试

测试编解码质量，计算原始帧与重建帧的 PSNR（峰值信噪比）。

**用法**：
```bash
./examples/bin/example_psnr_test -i <input.h265> [-b bitrate] [-v] [-n count]
```

**参数说明**：
- `-i` — 输入视频文件
- `-b` — 重编码码率（默认 4Mbps）
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

**源码位置**：`examples/psnr_test.c`

---

### visual_compare - SDL2 可视化质量预览

左侧显示输入原始解码帧，右侧显示重新编码并解码后的预览画面；底部实时显示码率、压缩比、延迟、稳定性和 PSNR。

**用法**：
```bash
./examples/bin/example_visual_compare -i <input.h265> [-b bitrate] [-n count] [-l] [-f]
```

**示例**：
```bash
./examples/bin/example_visual_compare -i video.h265 -b 4000000 -n 300 -l
```

该示例为可选 GUI 示例，构建时需要 SDL2 development package；未检测到 SDL2 时会自动跳过，不影响其他示例。

**源码位置**：`examples/visual_compare.c`

---

## 编译示例程序

所有示例程序源码位于 `examples/`，可以作为二次开发的参考。

**编译单个示例**：
```bash
export PKG_CONFIG_PATH=$PWD/share/pkgconfig:$PKG_CONFIG_PATH
export LD_LIBRARY_PATH=$PWD/lib:$LD_LIBRARY_PATH

gcc -o my_encoder examples/encode_file.c $(pkg-config --cflags --libs rkvc)
```

如果编译时给自定义程序写入了指向包内 `lib/` 的 RPATH，运行时可不设置 `LD_LIBRARY_PATH`。

**运行自编译程序**：
```bash
./my_encoder -o output.h265 -s 1920x1080 -n 300
```

详见 [二次开发指南](DEVELOPMENT.md)。
