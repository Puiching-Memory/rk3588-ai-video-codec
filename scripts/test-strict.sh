#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

run_matrix() {
    local preset="$1"
    local build_dir="$2"

    rm -rf "$ROOT_DIR/$build_dir"

    echo "==> configure: $preset"
    cmake --preset "$preset"

    echo "==> build: $preset"
    cmake --build --preset "$preset" -j"$(nproc)"

    echo "==> test: $preset"
    ctest --preset "$preset"
}

cd "$ROOT_DIR"

run_matrix tests build-tests
run_matrix asan build-asan
run_matrix coverage build-coverage

if command -v valgrind >/dev/null 2>&1; then
    echo "==> valgrind: build-tests"
    valgrind --quiet --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./build-tests/test_types
    valgrind --quiet --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./build-tests/test_frame
    valgrind --quiet --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./build-tests/test_contracts
else
    echo "==> skipping valgrind (not installed)"
fi

if command -v gcovr >/dev/null 2>&1; then
    echo "==> coverage summary"
    gcovr --root "$ROOT_DIR" "$ROOT_DIR/build-coverage"
else
    echo "==> skipping gcovr (not installed)"
fi