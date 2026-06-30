#!/bin/bash
# scripts/portable-test-helpers.sh — 可移植包测试共用辅助函数

generate_raw_nv12() {
    local path="$1" w="$2" h="$3" n="${4:-10}"
    local y_plane=$((w * h))
    local uv_plane=$((y_plane / 2))
    local i

    : > "$path"
    for ((i = 0; i < n; i++)); do
        dd if=/dev/zero bs="$y_plane" count=1 status=none 2>/dev/null >> "$path"
        dd if=/dev/zero bs="$uv_plane" count=1 status=none 2>/dev/null >> "$path"
    done
}

encode_test_clip() {
    local pkg_dir="$1" out="$2" size="$3" frames="${4:-10}" bitrate="${5:-1000000}"
    local example="$pkg_dir/examples/bin/example_encode_file"

    if [[ -x "$example" ]]; then
        env LD_LIBRARY_PATH="$pkg_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
            "$example" -o "$out" -s "$size" -n "$frames" -b "$bitrate"
        return $?
    fi

    local w="${size%x*}" h="${size#*x}"
    local raw="$out.raw.nv12"
    generate_raw_nv12 "$raw" "$w" "$h" "$frames"
    env LD_LIBRARY_PATH="$pkg_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$pkg_dir/bin/rkvc_encode" -i "$raw" -o "$out" -s "$size" -p realtime
}
