# rkvc — RK3588 视频编解码库

高性能硬件视频编解码库，专为 RK3588 平台优化。

## 功能特性

- **硬件视频编码** — 支持 8K 分辨率，异步编码
- **硬件视频解码** — 支持 8K 10-bit 解码
- **离线处理** — 文件输入/输出，自动封装/解封装
- **实时流式处理** — 帧级 push/pull API，适合监控/推流场景
- **硬件加速缩放** — 零 CPU 占用的分辨率转换和格式转换

## 性能

| 测试类型 | 帧率     | 实时倍率 |
| -------- | -------- | -------- |
| 视频编码 | ~290 fps | ~9.6x    |
| 视频解码 | ~270 fps | ~9.0x    |
| 流式编码 | ~215 fps | ~7.2x    |

## 环境要求

- Rockchip RK3588 平台
- Linux 内核 5.10 或更高版本

## 设备权限

运行前需要设置设备权限。如遇权限错误，请联系技术支持配置。

## 快速使用

### 硬件能力查询

```bash
rkvc_info
```

### 编码示例

```bash
# 测试图案编码
rkvc_encode --testsrc -o test.h265 -s 1920x1080 -n 100
```

### 解码示例

```bash
# 解码到原始 NV12 格式
rkvc_decode -i test.h265 -o decoded.nv12
```

### 管道模式

```bash
# 编码 → 解码管道
rkvc_encode --testsrc --stdout -s 640x480 -n 30 | \
  rkvc_decode --stdin --stdout -s 640x480 | wc -c
```

## 示例程序

本包包含以下示例程序源码和二进制：

- `encode_file` — 文件编码示例
- `decode_file` — 文件解码示例
- `stream_encode` — 流式编码示例
- `stream_decode` — 流式解码示例
- `transcode` — 转码示例
- `stream_transcode` — 流式转码示例
- `stream_device_pair` — 双设备流式传输示例
- `latency_test` — 端到端延迟测试
- `psnr_test` — 编解码质量测试

示例程序位于 `examples/` 目录。

## 开发集成

### pkg-config 方式

```bash
gcc -o myapp myapp.c $(pkg-config --cflags --libs rkvc)
```

### CMake 方式

```cmake
find_package(rkvc REQUIRED)
target_link_libraries(myapp PRIVATE rkvc::shared)
```

## 技术支持

如有问题或需要技术支持，请联系供应商。
