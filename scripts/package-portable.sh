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
# 前置依赖: gcc, make, pkg-config, ninja, patchelf, libdrm-dev

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FFMPEG_SRC="$PROJECT_DIR/third_party/ffmpeg-rockchip"
FFMPEG_PREFIX="$PROJECT_DIR/build-deps/ffmpeg-install"
RKVC_BUILD="$PROJECT_DIR/build"
OUT_DIR="$PROJECT_DIR/build/portable"

VERSION="$(grep -A1 'project(rkvc' "$PROJECT_DIR/CMakeLists.txt" | grep 'VERSION' | grep -oP '[0-9]+\.[0-9]+\.[0-9]+')"
ARCH="$(uname -m)"
PKG_NAME="rkvc-${VERSION}-linux-${ARCH}-portable"

CLEAN=0
[[ "${1:-}" == "--clean" ]] && CLEAN=1

check_deps() {
    local missing=()
    for cmd in gcc make pkg-config ninja patchelf; do
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

    cmake -B "$RKVC_BUILD" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON \
        -DFFMPEG_ROOT="$FFMPEG_PREFIX" \
        "$PROJECT_DIR"

    ninja -C "$RKVC_BUILD"
    echo "--- rkvc 构建完成 ---"
}

package() {
    echo ""
    echo "=== 打包 $PKG_NAME ==="

    rm -rf "$OUT_DIR/$PKG_NAME"
    mkdir -p "$OUT_DIR/$PKG_NAME"/{bin,lib,include/rkvc,share/pkgconfig}

    for tool in rkvc_encode rkvc_decode rkvc_info rkvc_bench; do
        [[ -f "$RKVC_BUILD/$tool" ]] && cp "$RKVC_BUILD/$tool" "$OUT_DIR/$PKG_NAME/bin/"
    done

    for so in librkvc.so.0.1.0 librkvc.so.0 librkvc.so; do
        [[ -f "$RKVC_BUILD/$so" ]] && cp -a "$RKVC_BUILD/$so" "$OUT_DIR/$PKG_NAME/lib/"
    done

    echo "--- 复制 ffmpeg 动态库 (仅限自行编译产出) ---"
    for lib in "$FFMPEG_PREFIX/lib/"lib*.so.*; do
        [[ -f "$lib" ]] || continue
        [[ -L "$lib" ]] && continue
        cp "$lib" "$OUT_DIR/$PKG_NAME/lib/"
        echo "  $(basename "$lib")"
    done

    cd "$OUT_DIR/$PKG_NAME/lib"
    for real in lib*.so.*; do
        [[ -f "$real" ]] || continue
        base="$(echo "$real" | sed 's/\.\([0-9]*\)\.[0-9]*\.[0-9]*$/.\1/')"
        short="$(echo "$real" | sed 's/\..*//')"
        ln -sf "$real" "$base" 2>/dev/null || true
        ln -sf "$base" "$short" 2>/dev/null || true
    done
    cd "$PROJECT_DIR"

    echo "--- 设置 RPATH ---"
    for tool in "$OUT_DIR/$PKG_NAME/bin/"*; do
        patchelf --set-rpath '$ORIGIN/../lib' "$tool" && \
            echo "  $(basename "$tool")"
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

    cp "$PROJECT_DIR/README.md" "$OUT_DIR/$PKG_NAME/" 2>/dev/null || true
    cp "$PROJECT_DIR/LICENSE" "$OUT_DIR/$PKG_NAME/" 2>/dev/null || true

    cd "$OUT_DIR"
    tar czf "$PKG_NAME.tar.gz" "$PKG_NAME"
    echo "  产物: $OUT_DIR/$PKG_NAME.tar.gz ($(du -h "$PKG_NAME.tar.gz" | cut -f1))"

    echo "--- 验证自包含库 ---"
    local unresolved=0
    (cd "$PKG_NAME" && LD_LIBRARY_PATH=lib ldd bin/rkvc_info 2>&1) | while read -r line; do
        if echo "$line" | grep -q "not found"; then
            echo "  错误: $line"
            unresolved=1
        fi
    done

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
