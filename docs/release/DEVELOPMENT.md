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

使用可移植包自带工具和示例程序时通常无需设置 `LD_LIBRARY_PATH`。进行二次开发时，建议设置 `PKG_CONFIG_PATH`；如果自编译程序没有写入 RPATH，再设置 `LD_LIBRARY_PATH`：

```bash
export PKG_CONFIG_PATH=/path/to/rkvc/share/pkgconfig:$PKG_CONFIG_PATH
export LD_LIBRARY_PATH=/path/to/rkvc/lib:$LD_LIBRARY_PATH
```

可将这些命令添加到项目启动脚本中。

### 设备权限

确保应用程序有权限访问硬件设备。生产环境建议通过 udev 规则配置权限。
库会在硬件编解码启动前检测必要设备权限；权限不足时返回 `RKVC_ERR_PERMISSION`。

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

// 视频帧是不透明句柄，通过 rkvc_frame_* API 访问
typedef struct rkvc_frame rkvc_frame;

// 帧元数据
typedef struct {
    int          width;
    int          height;
    rkvc_pix_fmt format;
    int64_t      pts;
    int          key_frame;
} rkvc_frame_info;

// 编码后的压缩包
typedef struct {
    const uint8_t *data;
    int            size;
    int64_t        pts;
    int64_t        dts;
    int            key_frame;
    int64_t        pos;
} rkvc_packet;

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
    RKVC_ERR_INTERNAL,
    RKVC_ERR_PERMISSION,
    RKVC_ERR_FORMAT
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
#include <string.h>

static void fill_nv12_frame(rkvc_frame *frame, int width, int height, int idx)
{
    uint8_t *planes[4] = {0};
    int strides[4] = {0};
    rkvc_frame_get_data(frame, planes, strides);

    // 填充 Y 平面
    for (int y = 0; y < height; y++) {
        memset(planes[0] + y * strides[0], (uint8_t)((y + idx) & 0xff), width);
    }

    // 填充 UV 平面（NV12）
    for (int y = 0; y < height / 2; y++) {
        memset(planes[1] + y * strides[1], 128, width);
    }

    rkvc_frame_set_pts(frame, idx);
}

int main(void) {
    rkvc_err err;
    
    // 1. 初始化库
    err = rkvc_init();
    if (err != RKVC_OK) {
        fprintf(stderr, "Init failed: %s\n", rkvc_err_str(err));
        return 1;
    }
    
    // 2. 配置编码器
    rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.fps_num = 30;
    cfg.fps_den = 1;
    cfg.bitrate = 4000000;  // 4 Mbps
    cfg.gop_size = 30;
    cfg.input_format = RKVC_PIX_FMT_NV12;
    
    // 3. 打开编码器并直接输出到文件
    rkvc_encoder *enc = NULL;
    err = rkvc_encoder_open_file(&enc, &cfg, "output.h265");
    if (err != RKVC_OK) {
        fprintf(stderr, "Open encoder failed: %s\n", rkvc_err_str(err));
        return 1;
    }
    
    // 4. 编码循环
    for (int i = 0; i < 300; i++) {
        // 准备输入帧
        rkvc_frame *frame = NULL;
        err = rkvc_frame_alloc(&frame, 1920, 1080, RKVC_PIX_FMT_NV12);
        if (err != RKVC_OK) break;
        
        // 填充帧数据（从摄像头、文件等）
        fill_nv12_frame(frame, 1920, 1080, i);
        
        // 编码
        err = rkvc_encoder_send_frame(enc, frame);
        rkvc_frame_unref(frame);
        
        if (err != RKVC_OK && err != RKVC_ERR_AGAIN) {
            fprintf(stderr, "Encode failed: %s\n", rkvc_err_str(err));
            break;
        }

        // 文件模式下 receive_packet 会自动写入 output.h265
        rkvc_packet pkt;
        while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
        }
    }
    
    // 5. 关闭编码器会自动 drain 并写入文件 trailer
    rkvc_encoder_close(enc);
    
    // 6. 清理
    rkvc_deinit();
    
    return 0;
}
```

如果不想直接写文件，而是要在内存中获取 H.265 结果，请使用无文件模式：

```c
rkvc_encoder_open(&enc, &cfg);
rkvc_encoder_send_frame(enc, frame);

rkvc_packet pkt;
while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
    // pkt.data / pkt.size 是编码后的压缩数据
    // 如需长期保存，请在下一次 send/receive/close 前 memcpy 出来
}
```

### 编码器配置参数

```c
typedef struct {
    int            width;        // 宽度（必需）
    int            height;       // 高度（必需）
    int            fps_num;      // 帧率分子（默认 30）
    int            fps_den;      // 帧率分母（默认 1）
    int64_t        bitrate;      // 码率 bps（默认 4000000）
    int            gop_size;     // GOP 大小（默认 60）
    rkvc_pix_fmt   input_format; // 输入格式（默认 NV12）
    rkvc_preset    preset;       // 编码预设（默认 MEDIUM）
    rkvc_rc_mode   rc_mode;      // 码率控制（默认 CBR）
    int            qp;           // CQP 模式下的 QP 值
    int            profile;      // HEVC profile（0=自动）
    int            level;        // HEVC level（0=自动）
    int            num_b_frames; // B 帧数量（默认 0）
    int            threads;      // 线程数（0=自动）
} rkvc_encoder_config;
```

**参数建议**：
- `bitrate`：1080p 建议 4-8 Mbps，4K 建议 15-25 Mbps
- `gop_size`：通常设为帧率值（如 30fps → gop=30）
- 实时低延迟场景建议 `num_b_frames = 0`，并用较小的 `gop_size`

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
    
    // 2. 打开文件解码器
    rkvc_decoder *dec = NULL;
    rkvc_decoder_config cfg = rkvc_decoder_config_defaults();
    err = rkvc_decoder_open_file(&dec, &cfg, "input.h265");
    if (err != RKVC_OK) {
        fprintf(stderr, "Open decoder failed: %s\n", rkvc_err_str(err));
        return 1;
    }
    
    // 3. 解码循环
    while (1) {
        err = rkvc_decoder_read_packet(dec);
        if (err == RKVC_ERR_EOF)
            break;  // 文件结束
        if (err != RKVC_OK) {
            fprintf(stderr, "Read packet failed: %s\n", rkvc_err_str(err));
            break;
        }

        rkvc_frame *frame = NULL;
        while (rkvc_decoder_receive_frame(dec, &frame) == RKVC_OK) {
            rkvc_frame_info info;
            rkvc_frame_get_info(frame, &info);

            printf("Decoded frame: %dx%d, pts=%ld\n",
                   info.width, info.height, (long)info.pts);

            // 使用帧数据
            uint8_t *planes[4] = {0};
            int strides[4] = {0};
            rkvc_frame_get_data(frame, planes, strides);
            // planes[0] - Y 平面
            // planes[1] - UV 平面（NV12）

            rkvc_frame_unref(frame);
        }
    }
    
    // 4. 清理
    rkvc_decoder_close(dec);
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
#include <string.h>

int stream_encode_example(void) {
    rkvc_init();
    
    // 1. 创建编码流
    rkvc_stream_config cfg = rkvc_stream_config_defaults();
    cfg.direction = RKVC_STREAM_ENCODE;
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.fps_num = 30;
    cfg.fps_den = 1;
    cfg.bitrate = 4000000;
    cfg.input_format = RKVC_PIX_FMT_NV12;
    cfg.buffer_size = 10;  // 内部缓冲区大小
    
    rkvc_stream *stream = NULL;
    rkvc_stream_open(&stream, &cfg);
    
    // 2. 推送原始帧
    for (int i = 0; i < 300; i++) {
        rkvc_frame *in_frame = NULL;
        rkvc_frame_alloc(&in_frame, 1920, 1080, RKVC_PIX_FMT_NV12);
        
        // 填充帧数据...
        uint8_t *planes[4] = {0};
        int strides[4] = {0};
        rkvc_frame_get_data(in_frame, planes, strides);
        for (int y = 0; y < 1080; y++)
            memset(planes[0] + y * strides[0], (uint8_t)(i & 0xff), 1920);
        for (int y = 0; y < 1080 / 2; y++)
            memset(planes[1] + y * strides[1], 128, 1920);
        rkvc_frame_set_pts(in_frame, i);
        
        // 推送到编码器
        rkvc_stream_push(stream, in_frame);
        rkvc_frame_unref(in_frame);
        
        // 3. 拉取编码数据
        rkvc_packet pkt;
        while (rkvc_stream_pull(stream, &pkt, 0) == RKVC_OK) {
            // 处理编码数据（发送到网络、写入文件等）
            // fwrite(pkt.data, 1, pkt.size, fp);
        }
    }

    rkvc_stream_finish(stream);
    rkvc_packet pkt;
    while (rkvc_stream_pull(stream, &pkt, -1) == RKVC_OK) {
        // 取出剩余编码包
    }
    
    // 4. 清理
    rkvc_stream_close(stream);
    rkvc_deinit();
    return 0;
}
```

### 流式解码

```c
int stream_decode_example(void) {
    rkvc_init();
    
    // 1. 创建解码流
    rkvc_stream_config cfg = rkvc_stream_config_defaults();
    cfg.direction = RKVC_STREAM_DECODE;
    cfg.output_format = RKVC_PIX_FMT_NV12;
    cfg.buffer_size = 10;
    
    rkvc_stream *stream = NULL;
    rkvc_stream_open(&stream, &cfg);
    
    // 2. 推送编码数据（从网络、文件等）
    uint8_t *encoded_data = ...; // 编码数据
    int data_size = ...;
    int64_t pts = ...;
    
    rkvc_packet pkt = {
        .data = encoded_data,
        .size = data_size,
        .pts = pts,
        .dts = pts
    };
    rkvc_stream_push(stream, &pkt);
    
    // 3. 拉取解码帧
    rkvc_frame *out_frame = NULL;
    rkvc_err err = rkvc_stream_pull(stream, &out_frame, 0);
    if (err == RKVC_OK) {
        // 处理解码帧（显示、保存等）
        // ...
        rkvc_frame_unref(out_frame);
    }
    
    // 4. 清理
    rkvc_stream_close(stream);
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
    rkvc_frame *src = NULL;
    rkvc_frame_alloc(&src, 1920, 1080, RKVC_PIX_FMT_NV12);
    // ... 填充 src 数据 ...
    
    // 目标配置（720p）
    rkvc_scale_config cfg = {
        .dst_width = 1280,
        .dst_height = 720,
        .dst_format = RKVC_PIX_FMT_NV12
    };
    
    // 硬件缩放
    rkvc_frame *dst = NULL;
    rkvc_err err = rkvc_frame_scale(src, &dst, &cfg);
    if (err != RKVC_OK) {
        fprintf(stderr, "Scale failed: %s\n", rkvc_err_str(err));
    }
    
    // dst 现在包含缩放后的数据
    
    rkvc_frame_unref(src);
    rkvc_frame_unref(dst);
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
rkvc_err err = rkvc_encoder_send_frame(enc, frame);

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
    case RKVC_ERR_PERMISSION:
        // 设备权限不足，需检查部署权限
        fprintf(stderr, "Permission error: %s\n", rkvc_err_str(err));
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
| `RKVC_ERR_PERMISSION` | 设备权限不足           | 检查设备节点权限           |
| `RKVC_ERR_FORMAT`  | 输入格式不匹配            | 编码输入应为原始 NV12，压缩码流请先解码或转码 |
| `RKVC_ERR_HW`      | 硬件错误                  | 检查硬件状态，重启编解码器 |
| `RKVC_ERR_NOMEM`   | 内存不足                  | 减少缓冲区大小             |
| `RKVC_ERR_INVALID` | 参数错误                  | 检查配置参数               |

---

## 性能优化建议

### 1. 使用流式 API

流式 API 适合实时场景：
- 减少内存拷贝
- 更低延迟
- 更灵活的控制

### 2. 启用低延迟模式

实时应用建议关闭 B 帧，并使用解码低延迟模式：
```c
rkvc_encoder_config enc_cfg = rkvc_encoder_config_defaults();
enc_cfg.num_b_frames = 0;

rkvc_decoder_config dec_cfg = rkvc_decoder_config_defaults();
dec_cfg.low_delay = 1;
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
rkvc_scale_config cfg = {
    .dst_width = 1280,
    .dst_height = 720,
    .dst_format = -1
};
rkvc_frame *dst = NULL;
rkvc_frame_scale(src, &dst, &cfg);

// 差：软件缩放（CPU 占用高）
// software_scale(src, dst);
```

### 5. 零拷贝传递

在编解码链路中，尽量避免不必要的内存拷贝：
```c
// 解码 → 编码
rkvc_frame *frame = NULL;
while (rkvc_decoder_receive_frame(dec, &frame) == RKVC_OK) {
    rkvc_encoder_send_frame(enc, frame);
    rkvc_frame_unref(frame);
}
```

### 6. 批量处理

批量编码多帧可提高吞吐量：
```c
for (int i = 0; i < batch_size; i++) {
    rkvc_stream_push(stream, frames[i]);
}

rkvc_packet pkt;
while (rkvc_stream_pull(stream, &pkt, 0) == RKVC_OK) {
    // 处理编码包
}
```

---

## 常见问题

### Q1: 如何处理不同分辨率的输入？

使用硬件缩放统一到编码器期望的分辨率：
```c
rkvc_scale_config scfg = {
    .dst_width = target_width,
    .dst_height = target_height,
    .dst_format = RKVC_PIX_FMT_NV12
};
rkvc_frame *scaled = NULL;
rkvc_frame_scale(input, &scaled, &scfg);
rkvc_encoder_send_frame(enc, scaled);
rkvc_frame_unref(scaled);
```

### Q2: 如何实现多路编码？

创建多个编码器实例，每个处理一路流：
```c
rkvc_encoder *enc1, *enc2, *enc3;
rkvc_encoder_open_file(&enc1, &cfg1, "stream1.h265");
rkvc_encoder_open_file(&enc2, &cfg2, "stream2.h265");
rkvc_encoder_open_file(&enc3, &cfg3, "stream3.h265");
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

所有示例程序源码位于 `examples/`，可作为开发参考：

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
