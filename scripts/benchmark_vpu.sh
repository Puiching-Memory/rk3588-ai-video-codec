#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)

if ! command -v uv >/dev/null 2>&1; then
  echo "ERROR: 缺少命令: uv" >&2
  exit 1
fi

exec uv run --project "$REPO_ROOT" benchmark-vpu "$@"