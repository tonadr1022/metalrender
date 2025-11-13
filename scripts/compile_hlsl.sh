#!/bin/bash
set -euo pipefail

# glob all hlsl files in ./resources/shaders/hlsl and run this for each of them
SRC_DIR=resources/shaders/hlsl
shopt -s nullglob
HLSL_SOURCES=("$SRC_DIR"/*.hlsl)
shopt -u nullglob

if [[ ${#HLSL_SOURCES[@]} -eq 0 ]]; then
	echo "Error: no .hlsl files found in $SRC_DIR"
	exit 1
fi

for f in "${HLSL_SOURCES[@]}"; do
	echo "Compiling"
	./scripts/dxc_to_mtl.sh $f vert --metal
	./scripts/dxc_to_mtl.sh $f frag --metal
	./scripts/dxc_to_mtl.sh $f vert --spirv
	./scripts/dxc_to_mtl.sh $f frag --spirv
done
