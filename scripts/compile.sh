#!/bin/bash
set -euo pipefail

# Usage: ./compile.sh <output_dir> <shader_src_dir>
echo "Compiling shaders..."

if [[ $# -lt 1 ]]; then
	echo "Usage: $0 <output_dir> [shader_src_dir]"
	exit 1
fi

OUT_DIR="$1"
SRC_DIR="$2"

# Verify source dir exists
if [[ ! -d "$SRC_DIR" ]]; then
	echo "Error: shader source directory not found: $SRC_DIR"
	exit 1
fi

# Gather .metal sources
shopt -s nullglob
METAL_SOURCES=("$SRC_DIR"/*.metal)
shopt -u nullglob

if [[ ${#METAL_SOURCES[@]} -eq 0 ]]; then
	echo "Error: no .metal files found in $SRC_DIR"
	exit 1
fi

# Temp build dir for intermediates
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/metalbuild.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

echo "Sources:"
for f in "${METAL_SOURCES[@]}"; do
	echo "  - $f"
done

# Compile each .metal -> .air into BUILD_DIR
for src in "${METAL_SOURCES[@]}"; do
	base="$(basename "$src" .metal)"
	out_air="$BUILD_DIR/$base.air"
	echo "  metal -c $src -> $(basename "$out_air")"
	xcrun -sdk macosx metal -c "$src" -o "$out_air" -frecord-sources -gline-tables-only
done

# Link all .air -> metallib
mkdir -p "$OUT_DIR"
LIB_PATH="$OUT_DIR/default.metallib"

echo "Linking to $LIB_PATH"
xcrun -sdk macosx metallib -o "$LIB_PATH" "$BUILD_DIR"/*.air

echo "Done. Wrote: $LIB_PATH"
