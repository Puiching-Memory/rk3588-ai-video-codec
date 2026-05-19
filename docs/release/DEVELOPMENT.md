# 二次开发指南

本文档面向需要基于 rkvc 库进行二次开发的开发者，介绍如何集成 rkvc 到您的项目中。

## 目录

- [开发环境准备](#开发环境准备)
- [集成方式](#集成方式)
- [API 概览](#api-概览)
- [编码开发](#编码开发)
- [解码开发](#解码开发)
- [流式处理开发](#流式处理开发)
- [帧缩放开发](#帧缩放开发)
- [错误处理](#错误处理)
- [性能优化建议](#性能优化建议)
- [常见问题](#常见问题)

---

## 开发环境准备

### 系统要求

- RK3588 平台
- Linux 内核 5.10 或更高版本
- GCC 或 Clang（支持 C11）
- pkg-config 或 CMake

### 设置环境变量

使用可移植包时，需要设置以下环境变量：

```bash
export LD_LIBRARY_PATH=/path/to/rkvc/lib:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=/path/to/rkvc/share/pkgconfig:$PKG_CONFIG_PATH
```

建议将这些命令添加到 `~/.bashrc` 或项目启动脚本中。

### 设备权限

确保应用程序有权限访问硬件设备。生产环境建议通过 udev 规则配置权限。

---

## 集成方式

### 方式 1：pkg-config（推荐）

**编译命令**：
```bash
gcc -o myapp myapp.c $(pkg-config --cflags --libs rkvc)
```

**Makefile 示例**：
```makefile
CC = gcc
CFLAGS = $(shell pkg-config --cflags rkvc)
LDFLAGS = $(shell pkg-config --libs rkvc)

myapp: myapp.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
```

### 方式 2：CMake

**CMakeLists.txt 示例**：
```cmake
cmake_minimum_required(VERSION 3.16)
project(myapp C)

# 方法 A：使用 pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(RKVC REQUIRED rkvc)

add_executable(myapp myapp.c)
target_include_directories(myapp PRIVATE ${RKVC_INCLUDE_DIRS})
target_link_libraries(myapp PRIVATE ${RKVC_LIBRARIES})
target_link_directories(myapp PRIVATE ${RKVC_LIBRARY_DIRS})

# 方法 B：使用 CMake config（如果已安装）
# find_package(rkvc REQUIRED)
# add_executable(myapp myapp.c)
# target_link_libraries(myapp PRIVATE rkvc::shared)
```

### 方式 3：手动链接

```bash
gcc -o myapp myapp.c \
  -I/path/to/rkvc/include \
  -L/path/to/rkvc/lib \
  -lrkvc \
  -Wl,-rpath,/path/to/rkvc/lib
```

---

## API 概览

### 头文件

```c
#include <rkvc/rkvc.h>  // 主头文件，包含所有 API
```

### 核心数据结构

```c
// 编码器/解码器句柄
typedef struct rkvc_encoder rkvc_encoder;
typedef struct rkvc_decoder rkvc_decoder;

// 流式处理句柄
typedef struct rkvc_stream rkvc_stream;

// 视频帧
typedef struct {
    uint8_t *data[3];      // Y, U, V 平面数据指针
    int linesize[3];       // 每个平面的行字节数
    int width, height;     // 分辨率
    rkvc_pixel_format fmt; // 像素格式
    int64_t pts;           // 时间戳（微秒）
} rkvc_frame;

// 错误码
typedef enum {
    RKVC_OK = 0,
    RKVC_ERR_NOMEM,
    RKVC_ERR_INVALID,
    RKVC_ERR_NOT_FOUND,
    RKVC_ERR_IO,
    RKVC_ERR_HW,
    RKVC_ERR_EOF,
    RKVC_ERR_AGAIN,
    RKVC_ERR_MUX,
    RKVC_ERR_INTERNAL
} rkvc_err;
```

### 初始化与清理

```c
// 库初始化（线程安全，只需调用一次）
rkvc_err rkvc_init(void);

// 库清理
void rkvc_deinit(void);

// 查询版本
const char *rkvc_version(void);
uint32_t rkvc_version_number(void);

// 错误码转字符串
const char *rkvc_err_str(rkvc_err err);
```

---

## 编码开发

### 基本编码流程

```c
#include <rkvc/rkvc.h>
#include <stdio.h>

int main(void) {
    rkvc_err err;
    
    // 1. 初始化库
    err = rkvc_init();
    if (err != RKVC_OK) {
        fprintf(stderr, "Init failed: %s\n", rkvc_err_str(err));
        return 1;
    }
    
    // 2. 配置编码器
    rkvc_encoder_config cfg = {
        .width = 1920,
        .height = 1080,
        .fps = 30,
        .bitrate = 4000000,  // 4 Mbps
        .gop_size = 30,
        .input_format = RKVC_PIX_FMT_NV12
    };
    
    // 3. 创建编码器
    rkvc_encoder *enc = NULL;
    err = rkvc_encoder_create(&enc, &cfg, "output.h265");
    if (err != RKVC_OK) {
        fprintf(stderr, "Create encoder failed: %s\n", rkvc_err_str(err));
        return 1;
    }
    
    // 4. 编码循环
    for (int i = 0; i < 300; i++) {
        // 准备输入帧
        rkvc_frame frame;
        err = rkvc_frame_alloc(&frame, 1920, 1080, RKVC_PIX_FMT_NV12);
        if (err != RKVC_OK) break;
        
        // 填充帧数据（从摄像头、文件等）
        // ... fill frame.data[0], frame.data[1] ...
        frame.pts = i * 1000000 / 30;  // 微秒
        
        // 编码
        err = rkvc_encoder_encode(enc, &frame);
        rkvc_frame_free(&frame);
        
        if (err != RKVC_OK && err != RKVC_ERR_AGAIN) {
            fprintf(stderr, "Encode failed: %s\n", rkvc_err_str(err));
            break;
        }
    }
    
    // 5. 刷新编码器（输出缓冲帧）
    rkvc_encoder_flush(enc);
    
    // 6. 清理
    rkvc_encoder_destroy(enc);
    rkvc_deinit();
    
    return 0;
}
```

### 编码器配置参数

```c
typedef struct {
    int width;                    // 宽度（必需）
    int height;                   // 高度（必需）
    int fps;                      // 帧率（默认 30）
    int bitrate;                  // 码率 bps（默认 4000000）
    int gop_size;                 // GOP 大小（默认 30）
    rkvc_pixel_format input_format; // 输入格式（默认 NV12）
    int low_latency;              // 低延迟模式（0/1，默认 0）
} rkvc_encoder_config;
```

**参数建议**：
- `bitrate`：1080p 建议 4-8 Mbps，4K 建议 15-25 Mbps
- `gop_size`：通常设为帧率值（如 30fps → gop=30）
- `low_latency`：实时应用设为 1，离线处理设为 0

---

## 解码开发

### 基本解码流程

```c
#include <rkvc/rkvc.h>
#include <stdio.h>

int main(void) {
    rkvc_err err;
    
    // 1. 初始化
    err = rkvc_init();
    if (err != RKVC_OK) return 1;
    
    // 2. 创建解码器
    rkvc_decoder *dec = NULL;
    err = rkvc_decoder_create(&dec, "input.h265");
    if (err != RKVC_OK) {
        fprintf(stderr, "Create decoder failed: %s\n", rkvc_err_str(err));
        return 1;
    }
    
    // 3. 解码循环
    rkvc_frame frame;
    while (1) {
        err = rkvc_decoder_decode(dec, &frame);
        
        if (err == RKVC_ERR_EOF) {
            break;  // 文件结束
        }
        
        if (err == RKVC_ERR_AGAIN) {
            continue;  // 需要更多输入
        }
        
        if (err != RKVC_OK) {
            fprintf(stderr, "Decode failed: %s\n", rkvc_err_str(err));
            break;
        }
        
        // 处理解码帧
        printf("Decoded frame: %dx%d, pts=%ld\n", 
               frame.width, frame.height, frame.pts);
        
        // 使用帧数据...
        // frame.data[0] - Y 平面
        // frame.data[1] - UV 平面（NV12）
        
        // 释放帧
        rkvc_frame_free(&frame);
    }
    
    // 4. 清理
    rkvc_decoder_destroy(dec);
    rkvc_deinit();
    
    return 0;
}
```

---

## 流式处理开发

流式 API 适用于实时场景，提供更灵活的帧级控制。

### 流式编码

```c
#include <rkvc/rkvc.h>

int stream_encode_example(void) {
    rkvc_init();
    
    // 1. 创建编码流
    rkvc_stream_config cfg = {
        .mode = RKVC_STREAM_ENCODE,
        .width = 1920,
        .height = 1080,
        .fps = 30,
        .bitrate = 4000000,
        .input_format = RKVC_PIX_FMT_NV12,
        .buffer_size = 10  // 内部缓冲区大小
    };
    
    rkvc_stream *stream = NULL;
    rkvc_stream_create(&stream, &cfg);
    
    // 2. 推送原始帧
    for (int i = 0; i < 300; i++) {
        rkvc_frame in_frame;
        rkvc_frame_alloc(&in_frame, 1920, 1080, RKVC_PIX_FMT_NV12);
        
        // 填充帧数据...
        in_frame.pts = i * 1000000 / 30;
        
        // 推送到编码器
        rkvc_stream_push(stream, &in_frame);
        rkvc_frame_free(&in_frame);
        
        // 3. 拉取编码数据
        uint8_t *pkt_data = NULL;
        size_t pkt_size = 0;
        int64_t pkt_pts = 0;
        
        rkvc_err err = rkvc_stream_pull(stream, &pkt_data, &pkt_size, &pkt_pts);
        if (err == RKVC_OK) {
            // 处理编码数据（发送到网络、写入文件等）
            // fwrite(pkt_data, 1, pkt_size, fp);
            free(pkt_data);
        }
    }
    
    // 4. 清理
    rkvc_stream_destroy(stream);
    rkvc_deinit();
    return 0;
}
```

### 流式解码

```c
int stream_decode_example(void) {
    rkvc_init();
    
    // 1. 创建解码流
    rkvc_stream_config cfg = {
        .mode = RKVC_STREAM_DECODE,
        .buffer_size = 10
    };
    
    rkvc_stream *stream = NULL;
    rkvc_stream_create(&stream, &cfg);
    
    // 2. 推送编码数据（从网络、文件等）
    uint8_t *encoded_data = ...; // 编码数据
    size_t data_size = ...;
    int64_t pts = ...;
    
    rkvc_stream_push_data(stream, encoded_data, data_size, pts);
    
    // 3. 拉取解码帧
    rkvc_frame out_frame;
    rkvc_err err = rkvc_stream_pull_frame(stream, &out_frame);
    if (err == RKVC_OK) {
        // 处理解码帧（显示、保存等）
        // ...
        rkvc_frame_free(&out_frame);
    }
    
    // 4. 清理
    rkvc_stream_destroy(stream);
    rkvc_deinit();
    return 0;
}
```

---

## 帧缩放开发

rkvc 提供基于硬件加速的零 CPU 占用帧缩放功能。

### 缩放 API

```c
#include <rkvc/rkvc.h>

int scale_example(void) {
    rkvc_init();
    
    // 源帧（1080p）
    rkvc_frame src;
    rkvc_frame_alloc(&src, 1920, 1080, RKVC_PIX_FMT_NV12);
    // ... 填充 src 数据 ...
    
    // 目标帧（720p）
    rkvc_frame dst;
    rkvc_frame_alloc(&dst, 1280, 720, RKVC_PIX_FMT_NV12);
    
    // 硬件缩放
    rkvc_err err = rkvc_frame_scale(&src, &dst);
    if (err != RKVC_OK) {
        fprintf(stderr, "Scale failed: %s\n", rkvc_err_str(err));
    }
    
    // dst 现在包含缩放后的数据
    // dst.pts 自动从 src.pts 复制
    
    rkvc_frame_free(&src);
    rkvc_frame_free(&dst);
    rkvc_deinit();
    return 0;
}
```

**支持的缩放类型**：
- 下采样（downscaling）：4K → 1080p, 1080p → 720p
- 上采样（upscaling）：720p → 1080p
- 格式转换：NV12 ↔ YUV420P, NV16 ↔ P010

**性能**：硬件加速缩放，零 CPU 占用，延迟 < 1ms

---

## 错误处理

### 错误码检查

```c
rkvc_err err = rkvc_encoder_encode(enc, &frame);

if (err != RKVC_OK) {
    switch (err) {
    case RKVC_ERR_AGAIN:
        // 缓冲区满，稍后重试
        break;
    case RKVC_ERR_EOF:
        // 流结束
        break;
    case RKVC_ERR_HW:
        // 硬件错误，可能需要重启编码器
        fprintf(stderr, "Hardware error: %s\n", rkvc_err_str(err));
        break;
    default:
        fprintf(stderr, "Error: %s\n", rkvc_err_str(err));
        break;
    }
}
```

### 常见错误处理

| 错误码             | 含义                      | 处理建议                   |
| ------------------ | ------------------------- | -------------------------- |
| `RKVC_ERR_AGAIN`   | 需要更多输入/输出缓冲区满 | 稍后重试                   |
| `RKVC_ERR_EOF`     | 流结束                    | 正常结束                   |
| `RKVC_ERR_HW`      | 硬件错误                  | 检查设备权限，重启编解码器 |
| `RKVC_ERR_NOMEM`   | 内存不足                  | 减少缓冲区大小             |
| `RKVC_ERR_INVALID` | 参数错误                  | 检查配置参数               |

---

## 性能优化建议

### 1. 使用流式 API

流式 API 比文件 API 性能更好，适合实时场景：
- 减少内存拷贝
- 更低延迟
- 更灵活的控制

### 2. 启用低延迟模式

实时应用建议启用：
```c
cfg.low_latency = 1;
```

### 3. 合理设置缓冲区大小

```c
cfg.buffer_size = 10;  // 默认值，可根据场景调整
```
- 实时应用：5-10
- 离线处理：20-50

### 4. 使用硬件缩放

需要缩放时，使用 `rkvc_frame_scale()` 而非软件缩放：
```c
// 好：硬件缩放
rkvc_frame_scale(&src, &dst);

// 差：软件缩放（CPU 占用高）
// software_scale(&src, &dst);
```

### 5. 零拷贝传递

在编解码链路中，尽量避免不必要的内存拷贝：
```c
// 解码 → 编码（零拷贝）
rkvc_decoder_decode(dec, &frame);
rkvc_encoder_encode(enc, &frame);  // 直接传递，无拷贝
```

### 6. 批量处理

批量编码多帧可提高吞吐量：
```c
for (int i = 0; i < batch_size; i++) {
    rkvc_stream_push(stream, &frames[i]);
}
for (int i = 0; i < batch_size; i++) {
    rkvc_stream_pull(stream, ...);
}
```

---

## 常见问题

### Q1: 如何处理不同分辨率的输入？

使用硬件缩放统一到编码器期望的分辨率：
```c
rkvc_frame scaled;
rkvc_frame_alloc(&scaled, target_width, target_height, RKVC_PIX_FMT_NV12);
rkvc_frame_scale(&input, &scaled);
rkvc_encoder_encode(enc, &scaled);
```

### Q2: 如何实现多路编码？

创建多个编码器实例，每个处理一路流：
```c
rkvc_encoder *enc1, *enc2, *enc3;
rkvc_encoder_create(&enc1, &cfg1, "stream1.h265");
rkvc_encoder_create(&enc2, &cfg2, "stream2.h265");
rkvc_encoder_create(&enc3, &cfg3, "stream3.h265");
```

RK3588 支持多路并发编解码。

### Q3: 如何集成到现有项目？

rkvc 提供标准 C API，可以与其他视频处理库混合使用。参考示例程序了解集成方式。

### Q4: 线程安全吗？

- `rkvc_init()` / `rkvc_deinit()` 是线程安全的
- 每个编码器/解码器实例不是线程安全的，需要外部同步
- 不同实例之间可以并发使用

### Q5: 如何调试性能问题？

使用 `rkvc_bench` 工具建立性能基线：
```bash
./bin/rkvc_bench --quick
```

对比您的应用性能，定位瓶颈。

---

## 示例代码参考

所有示例程序源码位于 `examples/src/`，可作为开发参考：

- `encode_file.c` - 文件编码完整示例
- `decode_file.c` - 文件解码完整示例
- `stream_encode.c` - 流式编码示例
- `stream_decode.c` - 流式解码示例
- `transcode.c` - 转码示例
- `stream_device_pair.c` - 网络传输示例

建议从这些示例开始，根据需求修改。

---

## 技术支持

如有开发问题或需要技术支持，请联系供应商。
