# API 参考

!!! note
    完整的 API 文档请查看 Doxygen 生成的 HTML 参考：

    ```bash
    cmake -B build -DRKVC_BUILD_DOCS=ON
    cmake --build build --target docs
    # 输出在 build/docs/html/index.html
    ```

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

```c
rkvc_encoder *enc = NULL;
rkvc_encoder_config cfg = rkvc_encoder_config_defaults();
cfg.width = 1920; cfg.height = 1080;
cfg.bitrate = 4000000;

rkvc_encoder_open_file(&enc, &cfg, "output.h265");
rkvc_encoder_send_frame(enc, frame);
rkvc_encoder_close(enc);
```

## 解码器

```c
rkvc_decoder *dec = NULL;
rkvc_decoder_open_file(&dec, &rkvc_decoder_config_defaults(), "input.h265");

rkvc_frame *frame = NULL;
while (rkvc_decoder_read_packet(dec) == RKVC_OK)
    while (rkvc_decoder_receive_frame(dec, &frame) == RKVC_OK)
        rkvc_frame_unref(frame);

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
