# Changelog

本文档记录 rkvc 各版本的主要变更。

## [0.1.5] - 2026-06-18

### 发布重点

- 修复 `rkvc_frame_scale()` 在 1920×1080 NV12 帧底部产生 16 行纯绿色带的硬件错位 bug，根因是 ffmpeg `av_frame_get_buffer()` 与 RGA `wrapbuffer_virtualaddr_t()` 对 UV 平面偏移的假设不一致。
- 凡是高度不是 32 倍数的帧（典型如 1080p）经过 `rkvc_frame_scale` 都会受影响；修复后帧底字节级与输入一致，PSNR 从 24.27 dB 提升至 ∞（同分辨率缩放），全部图像内容完整保留。
- 新增 3 个回归测试钉住该问题，反向验证：移除修复后 3 个用例立即失败并精确定位错误位置。

### 修复

- **`rkvc_frame_scale` 帧底绿色带 (UV 错位)**
  - 根因：ffmpeg `av_frame_get_buffer(0)` 会按 32 行高度对齐 + 每平面 16 字节 padding，对 1920×1080 NV12 实际产生 `data[1] = data[0] + linesize[0]*1088 + 32` 的布局（Y 与 UV 之间存在 15392 字节 gap）。RGA `wrapbuffer_virtualaddr_t()` 用单一基址 + `wstride*hstride` 推算 UV 地址，没有字段表达这个 gap，导致 RGA 把 UV 写到错误偏移；最终在帧底 16 luma 行处 UV=(0,0) 显示为纯绿色。
  - 修复：`rkvc_frame_alloc()` 不再调用 `av_frame_get_buffer()`，改为 `av_image_get_buffer_size + av_buffer_alloc + av_image_fill_arrays(align=1)` 自行分配严格连续的像素缓冲，让 RGA 的偏移算式与实际内存严格一致。受影响调用方包括 `examples/transcode.c`、`lib/stream.c` 自动缩放路径。
  - 兼容性：对外仍是连续布局的 `AVFrame`，`rkvc_frame_get_data()` 行为不变；不需要修改任何调用者代码。
  - 性能：fast path 零损失（实测 7.23 ms / 1080p NV12 RGA scale 不变）；如果调用方传入的源帧带 ffmpeg padding（例如直接来自 `av_hwframe_transfer_data`），`rkvc_frame_scale` 会先做一次 `av_image_copy`（约 0.4 ms / 1080p）再喂 RGA，保证正确性。

### 测试

- **回归测试**
  - 新增 `test_frame_alloc_contiguous_layout`：纯 CPU 校验 `rkvc_frame_alloc` 输出的 NV12/YUV420P 帧 `data[1] == data[0] + linesize[0]*H`，覆盖 1080p / 480p / 720p / 1440p。
  - 新增 `test_scale_identity_byte_exact_nv12_1080p`：1920×1080 NV12 经 `rkvc_frame_scale` 同分辨率缩放后必须与输入逐字节一致；用线性同余生成的随机 UV 内容，避免常数色块掩盖错位；额外检查帧底 16 行不存在 UV=(0,0) 全零行。
  - 新增 `test_scale_identity_byte_exact_yuv420p_1080p`：同上但用三平面 I420，覆盖 V 平面同样的偏移问题。
  - 反向验证：临时撤销修复，仅这 3 个新用例失败并报告具体错误位置（如 `[UV] mismatch row 531 col 1888: got=0 ref=41`），其余 11 个用例继续通过。

### 内部

- 新增 `rkvc_avframe_alloc_contiguous(AVFrame*)` 内部 helper，封装"严格连续无 padding"的 ffmpeg 帧缓冲分配。
- 新增 `frame_is_contiguous_for_rga(AVFrame*)` 检查，识别外部传入帧是否安全直接喂 RGA，覆盖 NV12/NV21/NV16/YUV420P/P010。

## [0.1.4] - 2026-06-05

### 发布重点

- 交付包从“能运行”升级为“可验证”：portable 包内新增一键自测与本机网络端到端回环，覆盖文件、依赖、RPATH、CLI、编解码、pkg-config、负向包结构和 UDP 网络链路。
- 硬件启动前增加设备权限门控，权限不足时返回明确错误，避免落入 RKMPP/FFmpeg 初始化后的不稳定路径。
- portable 包改为随包携带自建 rockchip-mpp 动态库，并通过 RPATH/RPATH-link 和自测防止解析到系统旧版 MPP。

### 新增

- **设备权限与输入格式错误码**
  - 新增 `RKVC_ERR_PERMISSION`，用于区分设备节点权限不足与一般硬件初始化失败；`rkvc_err_str()` 返回 `device permission denied`。
  - 新增 `RKVC_ERR_FORMAT` 和 `rkvc_probe_input_format()`，用于识别 H.264/H.265 Annex-B 与常见容器 magic，防止压缩码流被误当作原始 NV12 输入。

- **硬件权限前置检测**
  - RKMPP 编码/解码打开路径在 `avcodec_open2()` 前检查 `/dev/mpp_service` 和 MPP 实际优先使用的 DMA heap 子节点。
  - DMA heap 预检按 rockchip-mpp 默认选择顺序检查 `system-uncached`、`system`、`system-uncached-dma32`、`system-dma32`，避免目录可读但子节点不可读时进入 MPP 崩溃路径。
  - `rkvc_query_caps()` 现在按当前用户权限报告 MPP、DMA heap 和 RGA 能力。

- **可移植包一键自测与网络回环**
  - portable 包根目录新增 `test.sh`，可在包目录内直接执行完整验证。
  - 新增 `network-e2e-test.sh`，自动生成测试 H.265 码流，通过 `127.0.0.1` UDP/RTP 回环模拟网络传输，再由接收端解码并校验发送/接收帧数。
  - `scripts/test-portable.sh` 支持无参数包内运行，并把本机 UDP 网络端到端编解码回环纳入默认自测。

- **SDL2 可视化质量预览示例**
  - 新增 `examples/visual_compare.c`，并排展示输入原始帧与重新编码解码后的重建帧。
  - 底部实时显示码率、压缩比、端到端延迟、稳定性和 Y/U/V/加权 PSNR。
  - CMake 新增 `RKVC_BUILD_GUI_EXAMPLES` 选项；未检测到 SDL2 时自动跳过 GUI 示例，不增加核心库硬依赖。

### 修复

- **portable 包 MPP 运行库串入系统旧版本**
  - `librkvc.so` 现在保留对 `librockchip_mpp.so.1` 的直接运行时依赖，避免工具链接或运行时解析到系统旧版 MPP。
  - CMake 为库、工具、示例和测试目标加入本地依赖目录与 `rpath-link`，默认构建不再需要手动设置 `LD_LIBRARY_PATH` 才能解析 MPP 符号。
  - 打包校验新增“关键库必须解析到包内 `lib/`”检查，防止系统库误串入。

- **CLI 压缩输入误用**
  - `rkvc_encode -i` 现在会探测输入文件头；发现 H.265/H.264/MP4/MKV 等压缩视频时直接报错并提示改用解码或转码路径。
  - `rkvc_bench` 子测试失败时返回非 0，部署脚本和包自测可以可靠捕获失败。

### 变更

- **打包脚本更新** (`scripts/package-portable.sh`)
  - 自动从 `third_party/mpp` 构建并安装 rockchip-mpp，再用该 MPP 构建 ffmpeg-rockchip 和 rkvc。
  - 可移植包携带 `librockchip_mpp` / `librockchip_vpu` 动态库，并为这些库设置 `$ORIGIN` RPATH。
  - 打包时复制示例程序源码/二进制、发布文档、`test.sh` 和 `network-e2e-test.sh`。
  - 目标板前置依赖说明移除 `librockchip-mpp1`，仅保留系统 DRM/RGA 相关依赖；当前发布包大小约 2.5 MB。

- **构建与测试矩阵**
  - CMake 新增 `RKVC_BUILD_GUI_EXAMPLES`，SDL2 不存在时跳过 GUI 示例，不影响核心库、CLI 或其他示例。
  - 新增 `full-tests` preset，在单元测试基础上构建 CLI 工具并运行脚本回归。
  - 测试目标统一带上包内依赖路径，减少裸环境下 `LD_LIBRARY_PATH` 对测试结果的影响。

- **发布文档同步**
  - release README/USAGE/EXAMPLES 增加本机网络端到端测试命令。
  - `stream_device_pair` 文档参数更新为当前 CLI 的 `-c`、`--dst-ip`、`--dst-port`、`--bind-port`。
  - packaging/testing/delivery 文档同步 portable 包目录结构、MPP 动态库携带方式、RPATH 行为和当前自测覆盖范围。

### 测试

- 增加 `test_permissions`，通过 fake `/dev` 覆盖 `/dev/mpp_service`、MPP 默认 DMA heap、DRM fallback 和 `rkvc_query_caps()` 权限门控回归。
- 增加 `full-tests` CMake preset，并新增 `test_cli_args`、`test_bench_permission_failure` 两个 CTest 脚本目标。
- 增加流式 API 边界测试，覆盖统计值、重复 finish、finish 后 pull、`buffer_size` 上限等路径。
- 增加输入格式探测回归，覆盖 H.264/H.265 Annex-B、MP4 magic，以及编码器拒绝压缩码流误作为原始 NV12 输入的 SDK/CLI 路径。
- portable 包 `test.sh` 增加 pkg-config 最小程序编译运行、CLI 参数错误、不可执行工具、缺失/串入系统 MPP 库、绝对 RPATH 注入等负向测试。
- `network-e2e-test.sh` 已验证 UDP 与 RTP 本机回环；portable 包 `test.sh` 默认执行 UDP 端到端回环。
- 当前 `tests` preset 为 8 个 CTest 目标 / 68 个 CMocka 用例；`full-tests` 为 10 个 CTest 目标；portable 包自测当前 81 项全部通过。

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
