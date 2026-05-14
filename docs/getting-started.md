# 快速开始

## 环境要求

- Rockchip BSP 内核 (5.10 或 6.1)
- libdrm-dev、patchelf（可移植包打包依赖）
- CMake >= 3.16、Ninja（推荐）
- GCC / Clang（C11 支持）
- CMocka（测试框架，可选）

!!! note
    ffmpeg-rockchip 和 rockchip-mpp 均从 `third_party/` 子模块源码构建，无需系统预装。

## 设备权限

```bash
sudo chmod 666 /dev/mpp_service /dev/dma_heap /dev/rga
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
