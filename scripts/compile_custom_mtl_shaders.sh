#!/usr/bin/env bash
set -e

mkdir -p resources/shader_out/metal

OUTFILE1="resources/shader_out/metal/dispatch_indirect.metallib"
OUTFILE2="resources/shader_out/metal/dispatch_mesh.metallib"

if [ ! -f "$OUTFILE1" ]; then
	./scripts/compile_msl_to_metallib.sh \
		resources/shaders/indirect/dispatch_indirect.metal \
		"$OUTFILE1"
fi

if [ ! -f "$OUTFILE2" ]; then
	./scripts/compile_msl_to_metallib.sh \
		resources/shaders/indirect/dispatch_mesh.metal \
		"$OUTFILE2"
fi
