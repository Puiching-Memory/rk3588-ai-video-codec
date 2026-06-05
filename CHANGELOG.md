# Changelog

本文档记录 rkvc 各版本的主要变更。

## [0.1.4] - 2026-06-05

### 变更

- 版本号提升至 0.1.4
- 同步开发文档、发布文档和打包文档中的版本号与 API 示例
- 修正公共头文件注释中的编码、流式处理示例

## [0.1.3] - 2026-05-19

### 变更

- **发布文档优化**
  - 新增独立发布文档目录 `docs/release/`，包含 README、USAGE、EXAMPLES、DEVELOPMENT 四份用户文档
  - 移除发布包中的 LICENSE 文件
  - 发布文档采用通用技术描述（"硬件编解码"），移除具体实现细节（RKMPP/FFmpeg/RGA/H.265），保护知识产权
  - 命令示例和 API 参数保留真实文件扩展名（.h265）和参数值，确保事实准确性
  - 移除设备权限配置命令，改为引导用户联系技术支持
- **打包脚本更新** (`scripts/package-portable.sh`)
  - 自动复制 `docs/release/` 目录内容到发布包根目录
  - 不再复制 LICENSE 文件
- 版本号提升至 0.1.3

## [0.1.2] - 2026-05-18

### 新增

- **真实 UDP 网络传输** (`examples/stream_device_pair.c`)
  - 将原来的三种模拟模式（`ring`/`shm`/`rtp` 均为进程内内存模拟）全部替换为真实网络传输：
    - `udp` — 原始编码帧 over UDP Socket（16B 头: frag_id+frag_total+frame_len+pts, 大帧自动分片最多 16 片）
    - `rtp` — RTP 封包 over UDP Socket（H.265 NAL 分片 ≤1400B, Marker 位标记帧边界, SSRC）
  - 支持 `-r send|recv|both` 单角色/双角色部署模式，真正实现两台物理 RK3588 之间的流式传输
  - 一个板卡解码文件 + 重编码 → UDP 发送，另一个板卡 UDP 接收 + 实时解码
  - 提取通用 UDP Socket 辅助层，`udp` 和 `rtp` 两通道共享
- **UDP 大帧分片与重组**: IDR 帧可达 80–120 KB，超过单 UDP 数据报 65507 字节上限，`udp` 通道新增分片协议（`frag_id`/`frag_total`/`frag_mask` 位图去重），接收端自动组装交付
- **API 文档 UDP 传输须知**: `docs/api.md` 新增 warning 块，说明编码帧可能超过 UDP 数据报大小，给出分片协议头字段参考

### 修复

- **转码示例降分辨率失败**: `examples/transcode.c` 直接将解码帧送入不同分辨率的编码器导致 RKMPP 报 `invalid parameter`，修复为当分辨率不匹配时先用 `rkvc_frame_scale()` RGA 硬件缩放再送入编码器。
- **打包脚本符号链接**: `scripts/package-portable.sh` 修复动态库符号链接过多问题（如 `libavcodec.so.60.31` 中间层级），简化为标准 `libfoo.so → libfoo.so.X → libfoo.so.X.Y.Z`，`librkvc` 链接统一由循环生成。
- **测试脚本版本兼容**: `scripts/test-portable.sh` ffmpeg 库检查改为通配符匹配，不再硬编码具体版本号。

### 变更

- **打包脚本优化** (`scripts/package-portable.sh`)
  - 示例程序二进制（`example_*`）和源码（`examples/*.c`）自动打包到 `examples/bin/` 和 `examples/src/`
  - 示例二进制 RPATH 设置为 `$ORIGIN/../../lib`
- `rtp` 接收端设置 1 秒 `SO_RCVTIMEO` 超时，`finished` 标志改为 `__sync_synchronize` 保证跨线程可见

## [0.1.1] - 2026-05-17

### 新增

- **RGA 硬件缩放 API** (`rkvc_frame_scale`)
  - 基于 Rockchip RGA 2D 加速器，零 CPU 占用完成 NV12/YUV420P/NV16/P010 帧的缩放和格式转换
  - 支持 upscaling、downscaling、同分辨率复制
  - 自动保留源帧 PTS
  - 新增头文件 `include/rkvc/scale.h`，公共入口 `rkvc.h` 已自动包含

- **流式编码自动缩放**
  - `rkvc_stream_push()` 编码路径新增自动 RGA 缩放：当输入帧尺寸或像素格式与流配置不匹配时，内部自动调用 RGA 缩放/转换后再送入编码器
  - 无需调用方手动处理分辨率适配

- **示例程序**
  - `stream_transcode` — 流式转码管线示例（解码 → 自动缩放 → 编码），支持 `-s WxH` 参数指定输出分辨率
  - `stream_device_pair` — 双设备流式传输模拟示例，支持三种通道模式：
    - `ring` — 环形缓冲区（模拟 UDP 局域网）
    - `shm` — 共享内存 IPC（模拟 RTOS 零拷贝消息队列）
    - `rtp` — RTP/PS 封包（模拟 GB/T 28181 国标传输）

- **测试**
  - 新增 `test_scale` 测试套件（11 项），覆盖：
    - RGA 可用性检测
    - 参数校验（NULL 指针、零尺寸）
    - 硬件缩放（下采样、上采样、同分辨率、PTS 保留、1080p→720p 真实场景）
    - 流自动缩放集成测试

### 变更

- 构建系统新增链接 `rga`、`rt` 库
- `CMakeLists.txt` 示例目标列表新增 `stream_transcode`、`stream_device_pair`
- 测试目标列表新增 `test_scale`
- `.gitignore` 新增 `*.mp4`、`*.h264`、`*.h265` 规则，避免测试媒体文件入库

### 修复

- **P010 格式映射**：`rkvc_frame_scale` 的 P010 映射从错误的 8-bit NV12 (`RK_FORMAT_YCbCr_420_SP`) 修正为正确的 10-bit 格式 (`RK_FORMAT_YCbCr_420_SP_10B`)
- **流自动缩放目标格式**：`rkvc_stream_push()` 的缩放配置现在显式指定 `dst_format = s->config.input_format`，确保格式转换与编码器期望一致
- **示例错误处理**：`stream_device_pair` 发送端现在检查 `rkvc_stream_push` 返回值，失败时报错并终止

### CI

- **librga 依赖修复**：`librga-dev` 不在 Ubuntu 24.04 标准仓库中，改为从 `airockchip/librga` 上游仓库 clone 预编译头文件和 aarch64 动态库，修正头文件安装路径（`include/*.h` → `/usr/local/include/rga/`）
- **GitHub Actions 版本升级**：`actions/checkout` v4 → v6，`actions/upload-artifact` v4 → v7，解决 Node.js 20 废弃警告

### 打包

- 修复 `scripts/package-portable.sh`：
  - 自动检测 CMake 生成器（Ninja/Unix Makefiles），不再硬编码
  - `librkvc` 版本号改为通配符匹配，避免每次发版修改脚本
  - ffmpeg 库过滤为仅打包 libavcodec/libavformat/libavutil（去除 libavdevice/libavfilter/libpostproc）
  - 为 librkvc.so 和 ffmpeg 库设置 `$ORIGIN` RPATH，确保自包含
  - 使用独立 `build-portable/` 目录，避免与已有 `build/` 冲突
  - 验证步骤修复为 process substitution，正确报告未解析依赖

## [0.1.0] - 2026-05-14

### 新增

- 初始版本发布
- H.265 (HEVC) RKMPP 硬件编码器/解码器
- 流式 API（编码流/解码流，内部环形缓冲区，支持异步 push/pull）
- 文件模式（编解码器自动 mux/demux）
- CLI 工具（rkvc_encode、rkvc_decode、rkvc_info、rkvc_bench）
- 示例程序（encode_file、decode_file、stream_encode、stream_decode、transcode、latency_test、psnr_test）
- 运行时能力查询（`rkvc_query_caps`）
- 完整单元测试套件（CMocka）
- ASan/UBSan/覆盖率构建预设
- 可移植二进制包打包脚本
- CMake 构建系统，支持 pkg-config 和 CMake config 安装
