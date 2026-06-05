# 打包与分发

## 可移植包 (推荐)

从源码构建，核心运行库随包携带，解压即用：

```bash
# 确保子模块已初始化
git submodule update --init --depth 1

# 构建 (自动编译 rockchip-mpp + ffmpeg-rockchip + rkvc)
./scripts/package-portable.sh

# 测试
./scripts/test-portable.sh build/portable/rkvc-0.1.4-linux-aarch64-portable
```

产物：`rkvc-0.1.4-linux-aarch64-portable.tar.gz` (~2.5MB)

```
rkvc-0.1.4-linux-aarch64-portable/
├── bin/                     # 可执行工具 (RPATH 自包含)
│   ├── rkvc_encode
│   ├── rkvc_decode
│   ├── rkvc_info
│   └── rkvc_bench
├── lib/                     # 动态库
│   ├── librkvc.so*          # rkvc 库
│   ├── libavcodec.so.60     # ffmpeg-rockchip
│   ├── libavformat.so.60
│   ├── libavutil.so.58
│   ├── librockchip_mpp.so.1 # rockchip-mpp
│   ├── librockchip_vpu.so.1
│   └── ...                  # 不再携带 swscale/swresample 等未使用库
├── include/rkvc/            # 开发头文件
├── share/pkgconfig/rkvc.pc  # pkg-config
├── test.sh                  # 一键自测脚本
└── network-e2e-test.sh      # 本机网络端到端回环测试
```

### 使用

```bash
# 解压
tar xzf rkvc-0.1.4-linux-aarch64-portable.tar.gz
cd rkvc-0.1.4-linux-aarch64-portable

# 一键自测
./test.sh

# 本机网络端到端测试
./network-e2e-test.sh

# 运行工具
./bin/rkvc_info
./bin/rkvc_encode --testsrc -o test.h265 -s 1920x1080 -n 100

# 二次开发
gcc -o myapp myapp.c -Iinclude -Llib -lrkvc
LD_LIBRARY_PATH=lib ./myapp
```

包内工具和示例程序已设置 RPATH，会优先加载包内 `lib/` 中的 rkvc、ffmpeg-rockchip 和 rockchip-mpp 动态库；二次开发程序如未设置自己的 RPATH，可通过 `LD_LIBRARY_PATH=lib` 运行。

### 可复现性

所有二进制从源码构建：

- **rockchip-mpp**: `third_party/mpp/` 子模块，随包携带动态库
- **ffmpeg-rockchip**: `third_party/ffmpeg-rockchip/` 子模块，链接上述 MPP 构建
- **rkvc**: 项目源码
- 包内自测会确认关键库解析到包内 `lib/`，避免串入系统旧版本

## DEB 包

适用于系统级安装：

```bash
# 构建
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build package

# 安装
sudo dpkg -i build/packages/rkvc_0.1.4_arm64.deb
```

!!! note
    DEB 包依赖系统上的 ffmpeg-rockchip 和 librockchip-mpp (rockchip-mpp 从 `third_party/mpp/` 源码构建)。

## CPack TGZ

开发者 SDK 包（不含 ffmpeg 依赖）：

```bash
ninja -C build package
# 产物: build/packages/rkvc-0.1.4-Linux.tar.gz
```

## 打包脚本

| 脚本                                  | 用途                 |
| ------------------------------------- | -------------------- |
| `scripts/package-portable.sh`         | 从源码构建可移植包   |
| `scripts/package-portable.sh --clean` | 清理重建             |
| `scripts/test-portable.sh <dir>`      | 测试可移植包（当前 81 项，含网络回环和负向测试） |
| `<package>/test.sh`                   | 包内一键自测         |
| `<package>/network-e2e-test.sh`       | 本机 UDP/RTP 端到端编解码回环 |
