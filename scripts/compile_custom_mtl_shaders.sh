#!/usr/bin/env bash
mkdir -p resources/shader_out/metal
./scripts/compile_msl_to_metallib.sh resources/shaders/indirect/dispatch_indirect.metal resources/shader_out/metal/dispatch_indirect.metallib
./scripts/compile_msl_to_metallib.sh resources/shaders/indirect/dispatch_mesh.metal resources/shader_out/metal/dispatch_mesh.metallib
