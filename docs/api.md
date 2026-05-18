# API 参考

!!! note
    完整的 API 文档请查看 Doxygen 生成的 HTML 参考：

    ```bash
    cmake -B build -DRKVC_BUILD_DOCS=ON
    cmake --build build --target docs
    # 输出在 build/docs/html/index.html
    ```

    在线 Doxygen 文档：[API HTML 参考](https://puiching-memory.github.io/rk3588-ai-video-codec/doxygen/)

## 核心概念

### 错误处理

所有函数返回 `rkvc_err` 枚举：

```c
rkvc_err err = rkvc_encoder_open(&enc, &cfg);
if (err != RKVC_OK) {
    fprintf(stderr, "错误: %s\n", rkvc_err_str(err));
}
```

### 帧管理

`rkvc_frame` 使用引用计数管理生命周期：

```c
rkvc_frame *f = NULL;
rkvc_frame_alloc(&f, 1920, 1080, RKVC_PIX_FMT_NV12);
// 使用 f ...
rkvc_frame_unref(f);  // 用完必须 unref
```

## 编码器

### 离线文件编码

```c
rkvc_encoder *enc = NULL;
rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
cfg.width = 1920; cfg.height = 1080;
cfg.bitrate = 4000000;

rkvc_encoder_open_file(&enc, &cfg, "output.h265");
rkvc_encoder_send_frame(enc, frame);
rkvc_encoder_close(enc);
```

### 实时流编码 (无文件输出)

```c
rkvc_encoder_open(&enc, &cfg);  // 不带 _file

rkvc_encoder_send_frame(enc, frame);

rkvc_packet pkt;
while (rkvc_encoder_receive_packet(enc, &pkt) == RKVC_OK) {
    // pkt.data/pkt.size 有效，可发送到网络
}
rkvc_encoder_close(enc);
```

## 解码器

### 离线文件解码

```c
rkvc_decoder *dec = NULL;
rkvc_decoder_open_file(&dec, &rkvc_decoder_config_defaults(), "input.h265");

rkvc_frame *frame = NULL;
while (rkvc_decoder_read_packet(dec) == RKVC_OK)
    while (rkvc_decoder_receive_frame(dec, &frame) == RKVC_OK)
        rkvc_frame_unref(frame);

rkvc_decoder_close(dec);
```

### 实时流解码

```c
rkvc_decoder_open(&dec, &cfg);  // 不带 _file

rkvc_decoder_send_packet(dec, data, size, pts, dts);

rkvc_frame *frame = NULL;
while (rkvc_decoder_receive_frame(dec, &frame) == RKVC_OK) {
    // 处理解码帧
    rkvc_frame_unref(frame);
}
rkvc_decoder_close(dec);
```

## 流式 API

```c
rkvc_stream *s = NULL;
rkvc_stream_config scfg = rkvc_stream_config_defaults();
scfg.direction = RKVC_STREAM_ENCODE;
rkvc_stream_open(&s, &scfg);

rkvc_stream_push(s, frame);

rkvc_packet pkt;
while (rkvc_stream_pull(s, &pkt, 0) == RKVC_OK) { /* ... */ }

rkvc_stream_finish(s);
rkvc_stream_close(s);
```

!!! warning "UDP 传输须知：编码帧可能超过 UDP 数据报大小"
    硬件编码器输出的 **IDR 帧**（关键帧）可能达到 **80–120 KB**，远超单个 UDP 数据报上限 **65507 字节**。
    如果通过 UDP 传输 `rkvc_packet.data`，必须自行实现**分片与重组**。

    **示例**（参考 `examples/stream_device_pair.c`，通道 `udp`）：

    | 协议头结构        | 字段                     |
    | ----------------- | ------------------------ |
    | `frag_id` (2B)    | 分片序号 (network order) |
    | `frag_total` (2B) | 总分片数                 |
    | `frame_len` (4B)  | 完整帧总长               |
    | `pts` (8B)        | 时间戳 (big-endian)      |

    发送端按 `UDP_FRAG_PAYLOAD = 65491` 字节分片，接收端按 `frag_mask` 位图收集并去重，全部到达后组装交付。

## CLI 工具

### 编码

```bash
# 离线文件
rkvc_encode -i raw.nv12 -o out.h265 -s 1920x1080 -b 4M

# 测试图案
rkvc_encode --testsrc -o out.h265 -s 1920x1080 -n 300

# stdin 管道输入
cat raw.nv12 | rkvc_encode --stdin -o out.h265 -s 1920x1080

# stdout 管道输出 (用于管道连接)
rkvc_encode --testsrc --stdout -s 640x480 -n 30 > out.h265
```

### 解码

```bash
# 离线文件
rkvc_decode -i out.h265 -o decoded.nv12

# stdin 管道输入
rkvc_decode --stdin -s 1920x1080 -o decoded.nv12 < out.h265

# stdout 管道输出
rkvc_decode --stdin --stdout -s 1920x1080 < out.h265 > decoded.nv12
```

### 管道组合

```bash
# 编码 → 解码 一步到位
rkvc_encode --testsrc --stdout -s 640x480 -n 30 | \
  rkvc_decode --stdin --stdout -s 640x480 > decoded.nv12

# 验证: 编码→解码数据量应等于 W*H*1.5*帧数
rkvc_encode --testsrc --stdout -s 640x480 -n 10 | \
  rkvc_decode --stdin --stdout -s 640x480 | wc -c
# 预期: 640*480*1.5*10 = 4608000
```

### 硬件能力查询

```bash
rkvc_info            # 文本输出
rkvc_info --json     # JSON 输出 (适合脚本)
rkvc_info --version  # 版本号
```
