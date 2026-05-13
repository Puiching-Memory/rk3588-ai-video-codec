# 快速开始

## 环境要求

- Rockchip BSP 内核 (5.10 或 6.1)
- [ffmpeg-rockchip](https://github.com/nyanmisaka/ffmpeg-rockchip) 安装在 `/usr/local/ffmpeg-rockchip`
- libdrm-dev
- CMake >= 3.16
- Criterion (测试框架，可选)

## 设备权限

```bash
sudo chmod 666 /dev/mpp_service /dev/dma_heap /dev/rga
sudo chmod 666 /dev/dri/*
```

## 构建

```bash
# 使用 CMake Presets (推荐)
cmake --preset default
cmake --build build

# 或手动构建
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 运行测试

```bash
# 构建并运行单元测试
cmake -B build -DRKVC_BUILD_TESTS=ON
cmake --build build --target rkvc_tests
cd build && ctest --output-on-failure

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
