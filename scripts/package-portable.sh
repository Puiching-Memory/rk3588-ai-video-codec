#!/bin/bash
# scripts/package-portable.sh — 从源码构建可移植二进制包
#
# 用法:
#   ./scripts/package-portable.sh [--clean]
#
# 流程:
#   1. 从 third_party/ffmpeg-rockchip 子模块编译 ffmpeg
#   2. 用编译的 ffmpeg 构建 rkvc
#   3. bundle 动态库 + RPATH 打包
#
# 前置依赖: gcc, make, pkg-config, patchelf, libdrm-dev
# 可选依赖: ninja (若已有 ninja 构建目录则自动使用)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FFMPEG_SRC="$PROJECT_DIR/third_party/ffmpeg-rockchip"
FFMPEG_PREFIX="$PROJECT_DIR/build-deps/ffmpeg-install"
RKVC_BUILD="$PROJECT_DIR/build-portable"
OUT_DIR="$PROJECT_DIR/build/portable"

VERSION="$(grep -A1 'project(rkvc' "$PROJECT_DIR/CMakeLists.txt" | grep 'VERSION' | grep -oP '[0-9]+\.[0-9]+\.[0-9]+')"
ARCH="$(uname -m)"
PKG_NAME="rkvc-${VERSION}-linux-${ARCH}-portable"

CLEAN=0
[[ "${1:-}" == "--clean" ]] && CLEAN=1

# 自动检测 CMake 生成器: 若系统有 ninja 且 build 目录用 Ninja 则使用 Ninja，否则 Unix Makefiles
detect_generator() {
    local cache="$RKVC_BUILD/CMakeCache.txt"
    if [[ -f "$cache" ]] && grep -q "CMAKE_MAKE_PROGRAM.*ninja" "$cache"; then
        echo "Ninja"
    elif command -v ninja &>/dev/null; then
        echo "Ninja"
    else
        echo "Unix Makefiles"
    fi
}

detect_build_cmd() {
    local cache="$RKVC_BUILD/CMakeCache.txt"
    if [[ -f "$cache" ]] && grep -q "CMAKE_MAKE_PROGRAM.*ninja" "$cache"; then
        echo "ninja"
    else
        echo "make"
    fi
}

check_deps() {
    local missing=()
    for cmd in gcc make pkg-config patchelf; do
        command -v "$cmd" &>/dev/null || missing+=("$cmd")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        echo "错误: 缺少工具: ${missing[*]}"
        exit 1
    fi
    if [[ ! -f "$FFMPEG_SRC/configure" ]]; then
        echo "错误: 子模块未初始化，运行: git submodule update --init --depth 1"
        exit 1
    fi
}

build_ffmpeg() {
    echo "=== 构建 ffmpeg-rockchip ==="

    if [[ $CLEAN -eq 1 ]]; then
        rm -rf "$FFMPEG_PREFIX"
    fi

    if [[ -f "$FFMPEG_PREFIX/lib/libavcodec.so" ]]; then
        echo "--- ffmpeg 已构建，跳过 (用 --clean 重建) ---"
        return
    fi

    echo "--- 编译 ffmpeg ---"
    cd "$FFMPEG_SRC"

    ./configure \
        --prefix="$FFMPEG_PREFIX" \
        --enable-gpl --enable-version3 --enable-nonfree \
        --enable-rkmpp --enable-libdrm \
        --enable-pic \
        --enable-static --enable-shared \
        --disable-doc --disable-programs --disable-network \
        --disable-swscale --disable-swresample \
        --disable-x86asm \
        --disable-everything \
        --enable-encoder=hevc_rkmpp \
        --enable-decoder=hevc_rkmpp --enable-decoder=hevc \
        --enable-muxer=hevc --enable-muxer=matroska --enable-muxer=mp4 --enable-muxer=mpegts \
        --enable-demuxer=hevc --enable-demuxer=matroska --enable-demuxer=mov --enable-demuxer=mpegts \
        --enable-parser=hevc \
        --enable-protocol=file --enable-protocol=pipe \
        2>&1 | tail -3

    make -j"$(nproc)" 2>&1 | tail -3
    make install 2>&1 | tail -3

    echo "--- ffmpeg 构建完成 ---"
    cd "$PROJECT_DIR"
}

build_rkvc() {
    echo ""
    echo "=== 构建 rkvc ==="
    [[ $CLEAN -eq 1 ]] && rm -rf "$RKVC_BUILD"

    local gen
    gen="$(detect_generator)"
    echo "--- 使用生成器: $gen ---"

    cmake -B "$RKVC_BUILD" -G "$gen" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON \
        "$PROJECT_DIR"

    local build_cmd
    build_cmd="$(detect_build_cmd)"
    $build_cmd -C "$RKVC_BUILD"
    echo "--- rkvc 构建完成 ---"
}

package() {
    echo ""
    echo "=== 打包 $PKG_NAME ==="

    rm -rf "$OUT_DIR/$PKG_NAME"
    mkdir -p "$OUT_DIR/$PKG_NAME"/{bin,lib,include/rkvc,share/pkgconfig,examples/bin,examples/src}

    for tool in rkvc_encode rkvc_decode rkvc_info rkvc_bench; do
        [[ -f "$RKVC_BUILD/$tool" ]] && cp "$RKVC_BUILD/$tool" "$OUT_DIR/$PKG_NAME/bin/"
    done

    echo "--- 复制示例程序二进制 ---"
    for exe in "$RKVC_BUILD"/example_*; do
        [[ -f "$exe" ]] || continue
        cp "$exe" "$OUT_DIR/$PKG_NAME/examples/bin/"
        echo "  $(basename "$exe")"
    done

    echo "--- 复制示例程序源码 ---"
    for src in "$PROJECT_DIR/examples/"*; do
        [[ -f "$src" ]] || continue
        cp "$src" "$OUT_DIR/$PKG_NAME/examples/src/"
        echo "  $(basename "$src")"
    done

    # librkvc 版本号随项目变化，用通配符匹配
    local rkvc_real
    rkvc_real="$(ls -1 "$RKVC_BUILD"/librkvc.so.*.*.* 2>/dev/null | sort -V | tail -1)"
    if [[ -f "$rkvc_real" ]]; then
        cp -a "$rkvc_real" "$OUT_DIR/$PKG_NAME/lib/"
    fi
    # 符号链接由下方统一循环创建

    echo "--- 复制 ffmpeg 动态库 (仅限 rkvc 依赖) ---"
    # rkvc 只依赖 libavcodec / libavformat / libavutil
    for name in libavcodec libavformat libavutil; do
        # 取最大版本号的真实文件
        local lib
        lib="$(ls -1 "$FFMPEG_PREFIX/lib/${name}.so."* 2>/dev/null | grep -v '\.so$' | sort -V | tail -1)"
        [[ -f "$lib" ]] || continue
        [[ -L "$lib" ]] && continue
        cp "$lib" "$OUT_DIR/$PKG_NAME/lib/"
        echo "  $(basename "$lib")"
    done

    cd "$OUT_DIR/$PKG_NAME/lib"
    for real in lib*.so.*; do
        [[ -f "$real" ]] || continue
        [[ -L "$real" ]] && continue
        # 标准化两级链接 (符合 Linux ld.so 惯例):
        #   libfoo.so → libfoo.so.X           (dev 链接)
        #   libfoo.so.X → libfoo.so.X.Y.Z     (SONAME 链接, ld.so 运行时使用)
        soname="${real%.*.*}"                # libfoo.so.X
        dev="${real%%.so.*}.so"              # libfoo.so
        ln -sf "$real"   "$soname" 2>/dev/null || true
        ln -sf "$soname" "$dev"    2>/dev/null || true
    done
    cd "$PROJECT_DIR"

    echo "--- 设置 RPATH ---"
    for tool in "$OUT_DIR/$PKG_NAME/bin/"*; do
        patchelf --set-rpath '$ORIGIN/../lib' "$tool" && \
            echo "  $(basename "$tool")"
    done
    for exe in "$OUT_DIR/$PKG_NAME/examples/bin/"*; do
        [[ -f "$exe" ]] || continue
        patchelf --set-rpath '$ORIGIN/../../lib' "$exe" && \
            echo "  examples/bin/$(basename "$exe")"
    done
    # librkvc 依赖 ffmpeg 库，也需要 $ORIGIN RPATH
    local rkvc_so
    rkvc_so="$(ls -1 "$OUT_DIR/$PKG_NAME/lib"/librkvc.so.*.*.* 2>/dev/null | sort -V | tail -1)"
    if [[ -f "$rkvc_so" ]]; then
        patchelf --set-rpath '$ORIGIN' "$rkvc_so" && \
            echo "  $(basename "$rkvc_so")"
    fi
    # ffmpeg 自身库之间也有依赖 (avcodec → avutil)
    for lib in "$OUT_DIR/$PKG_NAME/lib/"libav*.so.*; do
        [[ -f "$lib" ]] || continue
        [[ -L "$lib" ]] && continue
        patchelf --set-rpath '$ORIGIN' "$lib" && \
            echo "  $(basename "$lib")"
    done

    cp "$PROJECT_DIR"/include/rkvc/*.h "$OUT_DIR/$PKG_NAME/include/rkvc/"
    cat > "$OUT_DIR/$PKG_NAME/share/pkgconfig/rkvc.pc" <<EOF
prefix=\${pcfiledir}/../..
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: rkvc
Description: RK3588 H.265 Video Codec Library (RKMPP)
Version: $VERSION
Libs: -L\${libdir} -lrkvc
Cflags: -I\${includedir}
EOF

    echo "--- 复制发布文档 ---"
    if [[ -d "$PROJECT_DIR/docs/release" ]]; then
        cp -r "$PROJECT_DIR/docs/release/"* "$OUT_DIR/$PKG_NAME/" 2>/dev/null || true
        ls "$OUT_DIR/$PKG_NAME"/*.md 2>/dev/null | while read -r f; do
            echo "  $(basename "$f")"
        done
    fi

    cd "$OUT_DIR"
    tar czf "$PKG_NAME.tar.gz" "$PKG_NAME"
    echo "  产物: $OUT_DIR/$PKG_NAME.tar.gz ($(du -h "$PKG_NAME.tar.gz" | cut -f1))"

    echo "--- 验证自包含库 ---"
    local unresolved=0
    while read -r line; do
        if echo "$line" | grep -q "not found"; then
            echo "  错误: $line"
            unresolved=1
        fi
    done < <(cd "$OUT_DIR/$PKG_NAME" && LD_LIBRARY_PATH=lib ldd bin/rkvc_info 2>&1)
    if [[ $unresolved -eq 0 ]]; then
        echo "  OK: 所有依赖已解析"
    else
        echo "  警告: 存在未解析依赖"
    fi

    echo "--- 目标板前置依赖 (须由系统包管理器提供) ---"
    echo "  librockchip-mpp1  (RKMPP 硬件编解码)"
    echo "  libdrm2           (DRM 渲染)"
    echo "  librga            (Rockchip 2D 加速, 可选)"
    echo ""
    echo "  安装示例: sudo apt install librockchip-mpp-dev libdrm-dev"
}

main() {
    echo "=== 可移植包构建 (rkvc $VERSION, $ARCH) ==="
    check_deps
    build_ffmpeg
    build_rkvc
    package
    echo ""
    echo "=== 完成: $OUT_DIR/$PKG_NAME.tar.gz ==="
}

main
