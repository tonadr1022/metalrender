#!/usr/bin/env bash
# Single entry point for agent validation: configure, build, compile shaders (changed-only when safe), optional format check.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Unstaged + staged + untracked paths, newline-separated, sorted.
changed_paths() {
	{
		git -C "$REPO_ROOT" diff --name-only
		git -C "$REPO_ROOT" diff --cached --name-only
		# Include newly-added (untracked) files, but respect .gitignore.
		git -C "$REPO_ROOT" ls-files -o --exclude-standard
	} | LC_ALL=C sort -u
}

PRESET="${CMAKE_PRESET:-Debug}"
BUILD_DIR="$REPO_ROOT/build/$PRESET"
BIN_DIR="$BUILD_DIR/bin"
TARGETS=(vktest teng-shaderc engine_scene_smoke)
DO_FORMAT=0
DO_TIDY=1

usage() {
	cat <<'EOF'
Usage: scripts/agent_verify.sh [options]

  Configures CMake, builds default targets, runs teng-shaderc on changed HLSL under
  resources/shaders/hlsl (entry points only), or --all if includes/shared headers changed.
  Optional: clang-format on changed first-party C/C++ headers/sources under apps/, src/, cmake/
  (unstaged + staged; not .mm/.m). Skips when the worktree is clean.
  Runs clang-tidy on changed first-party C/C++ sources/headers under apps/ and src/.

Options:
  --format          Run clang-format -Werror (needs clang-format on PATH, or CLANG_FORMAT)
  --skip-tidy       Skip clang-tidy (enabled by default)
  --preset NAME     CMake preset (default: Debug, or CMAKE_PRESET env)
  --skip-configure  Only build and shader compile (CMake must already be configured)
  -h, --help        This message

Environment:
  CMAKE_PRESET      Same as --preset
  CLANG_FORMAT      clang-format binary (default: clang-format)
  CLANG_TIDY        clang-tidy binary (default: clang-tidy)
  RUN_CLANG_TIDY    run-clang-tidy binary (default: run-clang-tidy, optional)
  VERIFY_TARGETS    Space-separated extra cmake --target names after defaults
EOF
}

SKIP_CONFIGURE=0
while [[ $# -gt 0 ]]; do
	case "$1" in
	--format)
		DO_FORMAT=1
		shift
		;;
	--skip-tidy)
		DO_TIDY=0
		shift
		;;
	--preset)
		PRESET="$2"
		BUILD_DIR="$REPO_ROOT/build/$PRESET"
		BIN_DIR="$BUILD_DIR/bin"
		shift 2
		;;
	--skip-configure)
		SKIP_CONFIGURE=1
		shift
		;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		echo "Unknown option: $1" >&2
		usage >&2
		exit 1
		;;
	esac
done

cd "$REPO_ROOT"

if [[ "$SKIP_CONFIGURE" -eq 0 ]]; then
	# cmake --preset "$PRESET"
	cmake --preset "$PRESET" 1>/dev/null
fi

extra_targets=()
if [[ -n "${VERIFY_TARGETS:-}" ]]; then
	read -r -a extra_targets <<<"$VERIFY_TARGETS"
fi

cmake --build "$BUILD_DIR" --target "${TARGETS[@]}" "${extra_targets[@]}"

"$BIN_DIR/engine_scene_smoke"

# clang-tidy on changed first-party sources/headers (apps/, src/). Uses compile_commands.json from build.
if [[ "$DO_TIDY" -eq 1 ]]; then
	CT="${CLANG_TIDY:-clang-tidy}"
	if ! command -v "$CT" >/dev/null 2>&1; then
		echo "agent_verify.sh: $CT not found; skipping clang-tidy (install clang-tidy or set CLANG_TIDY)" >&2
		DO_TIDY=0
	fi
fi

if [[ "$DO_TIDY" -eq 1 ]]; then
	"$SCRIPT_DIR/run_clang_tidy.sh"
fi

# Match apps/shaderc/main.cpp: only *.vert|frag|comp|mesh|task.hlsl are entry points; .hlsli/.h/etc. need --all.
SHADER_NEED_ALL=0
SHADER_ENTRIES=()
while IFS= read -r f; do
	[[ -n "$f" ]] || continue
	case "$f" in
	resources/shaders/hlsl/*) ;;
	*) continue ;;
	esac
	if [[ ! -f "$REPO_ROOT/$f" ]]; then
		SHADER_NEED_ALL=1
		continue
	fi
	case "$f" in
	*.vert.hlsl | *.frag.hlsl | *.comp.hlsl | *.mesh.hlsl | *.task.hlsl)
		SHADER_ENTRIES+=("$f")
		;;
	*)
		SHADER_NEED_ALL=1
		;;
	esac
done < <(changed_paths)

# Clean index + worktree (e.g. CI): compile everything. Otherwise only touch shaders when diffs say so.
if git -C "$REPO_ROOT" diff --quiet && git -C "$REPO_ROOT" diff --cached --quiet; then
	"$BIN_DIR/teng-shaderc" --all
elif [[ "$SHADER_NEED_ALL" -eq 1 ]]; then
	"$BIN_DIR/teng-shaderc" --all
elif [[ "${#SHADER_ENTRIES[@]}" -gt 0 ]]; then
	"$BIN_DIR/teng-shaderc" "${SHADER_ENTRIES[@]}"
else
	: # Dirty tree but no changes under resources/shaders/hlsl; skip shader compile
fi

if [[ "$DO_FORMAT" -eq 1 ]]; then
	CF="${CLANG_FORMAT:-clang-format}"
	if ! command -v "$CF" >/dev/null 2>&1; then
		echo "agent_verify.sh: $CF not found (install clang-format or set CLANG_FORMAT)" >&2
		exit 1
	fi
	# Only paths with unstaged or staged edits (agent/PR scope); not whole-repo ls-files.
	NEED_FORMAT=()
	while IFS= read -r f; do
		[[ -n "$f" ]] || continue
		case "$f" in
		apps/* | src/* | cmake/*) ;;
		*) continue ;;
		esac
		# Skip .mm/.m: root .clang-format is C++-only; clang-format errors on ObjC++.
		case "$f" in
		*.cpp | *.h | *.hpp | *.cc | *.cxx | *.inl) ;;
		*) continue ;;
		esac
		[[ -f "$REPO_ROOT/$f" ]] || continue
		if ! "$CF" --dry-run --Werror "$REPO_ROOT/$f" >/dev/null 2>&1; then
			NEED_FORMAT+=("$REPO_ROOT/$f")
		fi
	done < <(
		{
			git -C "$REPO_ROOT" diff --name-only --diff-filter=d
			git -C "$REPO_ROOT" diff --cached --name-only --diff-filter=d
			git -C "$REPO_ROOT" ls-files -o --exclude-standard
		} | LC_ALL=C sort -u
	)

	if [[ "${#NEED_FORMAT[@]}" -gt 0 ]]; then
		echo "agent_verify.sh: clang-format fixing ${#NEED_FORMAT[@]} file(s)"
		"$CF" -i "${NEED_FORMAT[@]}"
		# Re-check to catch tool/config errors deterministically.
		"$CF" --dry-run --Werror "${NEED_FORMAT[@]}"
	fi
fi

echo "agent_verify.sh: OK"
