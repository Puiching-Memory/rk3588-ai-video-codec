# 打包与分发

## 可移植包 (推荐)

从源码构建，包含所有运行时依赖，解压即用：

```bash
# 确保子模块已初始化
git submodule update --init --depth 1

# 构建 (自动编译 ffmpeg-rockchip + rkvc)
./scripts/package-portable.sh

# 测试
./scripts/test-portable.sh build/portable/rkvc-0.1.0-linux-aarch64-portable
```

产物：`rkvc-0.1.0-linux-aarch64-portable.tar.gz` (~7.4MB)

```
rkvc-0.1.0-linux-aarch64-portable/
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
│   └── ...                  # 不再携带 swscale/swresample 等未使用库
├── include/rkvc/            # 开发头文件
└── share/pkgconfig/rkvc.pc  # pkg-config
```

### 使用

```bash
# 解压
tar xzf rkvc-0.1.0-linux-aarch64-portable.tar.gz
cd rkvc-0.1.0-linux-aarch64-portable

# 运行工具
./bin/rkvc_info
./bin/rkvc_encode --testsrc -o test.h265 -s 1920x1080 -n 100

# 二次开发
gcc -o myapp myapp.c -Iinclude -Llib -lrkvc
LD_LIBRARY_PATH=lib ./myapp
```

### 可复现性

所有二进制从源码构建：

- **ffmpeg-rockchip**: `third_party/ffmpeg-rockchip/` 子模块 (shallow clone)
- **rkvc**: 项目源码
- 不依赖系统预装的任何库

## DEB 包

适用于系统级安装：

```bash
# 构建
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build package

# 安装
sudo dpkg -i build/packages/rkvc_0.1.0_arm64.deb
```

!!! note
    DEB 包依赖系统上的 ffmpeg-rockchip 和 librockchip-mpp。

## CPack TGZ

开发者 SDK 包（不含 ffmpeg 依赖）：

```bash
ninja -C build package
# 产物: build/packages/rkvc-0.1.0-Linux.tar.gz
```

## 打包脚本

| 脚本                                  | 用途                 |
| ------------------------------------- | -------------------- |
| `scripts/package-portable.sh`         | 从源码构建可移植包   |
| `scripts/package-portable.sh --clean` | 清理重建             |
| `scripts/test-portable.sh <dir>`      | 测试可移植包 (23 项) |
