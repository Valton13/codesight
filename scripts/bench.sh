#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
DIR="${1:-$ROOT/test_project}"
MAX="${2:-300}"
cmake --build build -j"$(nproc)" >/dev/null
python3 python/bench.py "$DIR" "$MAX"
