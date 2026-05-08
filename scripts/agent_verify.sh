#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export RUN_CLANG_TIDY="run-clang-tidy"
export CLANG_TIDY="clang-tidy"
exec python3 "$SCRIPT_DIR/agent_verify.py" "$@"
