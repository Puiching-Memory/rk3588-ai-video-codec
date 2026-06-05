# 快速开始

## 环境要求

- Rockchip BSP 内核 (5.10 或 6.1)
- libdrm-dev、patchelf（可移植包打包依赖）
- CMake >= 3.21、Ninja（推荐）
- GCC / Clang（C11 支持）
- CMocka（测试框架，可选）

!!! note
    ffmpeg-rockchip 和 rockchip-mpp 均从 `third_party/` 子模块源码构建，无需系统预装。

## 设备权限

运行时会检测当前用户是否可访问硬件编解码设备、MPP 默认 DMA heap 子节点和 RGA 设备；权限不足会返回 `RKVC_ERR_PERMISSION`。

```bash
sudo chmod 666 /dev/mpp_service /dev/dma_heap/* /dev/rga
sudo chmod 666 /dev/dri/*
```

## 构建

```bash
# 确保子模块已初始化
git submodule update --init --depth 1

# 使用 CMake Presets (推荐)
cmake --preset default
cmake --build build

# 或手动构建
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

## 运行测试

```bash
# 构建并运行单元测试
cmake --preset tests
cmake --build --preset tests
ctest --preset tests

# 运行基准测试
./build/rkvc_bench --quick
```

## 安装

```bash
cmake --install build --prefix /usr/local
```

安装后通过 pkg-config 使用：

```bash
gcc -o myapp myapp.c $(pkg-config --cflags --libs rkvc)
```

## 快速使用

```bash
# 硬件能力查询
rkvc_info

# 测试图案编码
rkvc_encode --testsrc -o test.h265 -s 1920x1080 -n 100

# 解码
rkvc_decode -i test.h265 -o decoded.nv12

# 管道模式: 编码 → 解码
rkvc_encode --testsrc --stdout -s 640x480 -n 30 | \
  rkvc_decode --stdin --stdout -s 640x480 | wc -c
# 预期: 640*480*1.5*30 = 13824000
```

## 延迟测试

模拟摄像头端到端延迟测试（编码 → 解码全链路）：

```bash
# 低延迟模式, 1080p@30fps
./example_latency_test -l

# 自定义分辨率和帧率
./example_latency_test -s 1280x720 -r 60 -l
```

输出逐帧延迟明细及统计摘要（P50/P95/P99）。详见 [基准测试](benchmark.md)。

## PSNR 质量测试

端到端编解码质量测试（解码 → 编码 → 重放解码 → 逐帧 PSNR 比较）：

```bash
# 基本用法
./example_psnr_test -i input.h265

# 逐帧 PSNR 明细
./example_psnr_test -i input.h265 -v -n 100
```

输出 Y/U/V 平均 PSNR、加权平均 PSNR 及最低帧 PSNR。详见 [基准测试](benchmark.md)。
