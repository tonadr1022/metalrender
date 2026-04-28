#!/usr/bin/env bash

# Run clang-tidy on changed first-party C/C++ sources/headers under apps/ and src/.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="$REPO_ROOT/build/Debug"
BIN_DIR="$BUILD_DIR/bin"

"$BIN_DIR/teng-shaderc" --all

if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo "agent_verify.sh: missing $BUILD_DIR/compile_commands.json (re-run without --skip-configure)" >&2
    exit 1
fi

TIDY_FILES=()
while IFS= read -r f; do
    [[ -n "$f" ]] || continue
    case "$f" in
    apps/*|src/*) ;;
    *) continue ;;
    esac
    case "$f" in
    *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx|*.inl) ;;
    *) continue ;;
    esac
    [[ -f "$REPO_ROOT/$f" ]] || continue
    TIDY_FILES+=("$REPO_ROOT/$f")
done < <(
    {
        git -C "$REPO_ROOT" diff --name-only --diff-filter=d
        git -C "$REPO_ROOT" diff --cached --name-only --diff-filter=d
        git -C "$REPO_ROOT" ls-files -o --exclude-standard
    } | LC_ALL=C sort -u
)

if [[ "${#TIDY_FILES[@]}" -gt 0 ]]; then
    RCT="${RUN_CLANG_TIDY:-run-clang-tidy}"
    CT="${CLANG_TIDY:-clang-tidy}"
    TIDY_CONFIG_ARGS=()
    if [[ -f "$REPO_ROOT/.clang-tidy" ]]; then
        TIDY_CONFIG_ARGS=(-config-file "$REPO_ROOT/.clang-tidy")
    fi
    if command -v "$RCT" >/dev/null 2>&1; then
        # run-clang-tidy understands compile_commands.json and parallelizes by default.
        "$RCT" -p "$BUILD_DIR" "${TIDY_CONFIG_ARGS[@]}" "${TIDY_FILES[@]}"
    else
        # Fallback: invoke clang-tidy per file (slower, but avoids extra dependency).
        for f in "${TIDY_FILES[@]}"; do
            "$CT" --config-file="$REPO_ROOT/.clang-tidy" -p "$BUILD_DIR" "$f"
        done
    fi
fi