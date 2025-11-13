#!/bin/bash

if [[ "$2" == *"vert"* ]]; then
	shader_model="vs_6_7"
	type="vert"
elif [[ "$2" == *"frag"* ]]; then
	shader_model="ps_6_7"
	type="frag"
else
	echo "unable to determine shader model from dxc output filename"
fi

if [[ "$3" == "--metal" ]]; then
	compile_metal=1
elif [[ "$3" == "--spirv" ]]; then
	compile_metal=0
fi

entry_point=$type\_main

full_path=$1
filename="${full_path##*/}"
basename="${filename%.*}"

if [[ ! -d "resources/shader_out/metal" ]]; then
	mkdir resources/shader_out/metal
fi

filepath=resources/shader_out/metal/$basename\_$type
metallib_path=$filepath.metallib
reflection_path=$filepath.json

echo "Compiling to $metallib_path"

if [[ $compile_metal == 1 ]]; then
	dxil_path=$filepath.dxil
	dxc $1 -Fo $dxil_path -T $shader_model -E $entry_point
	metal-shaderconverter $dxil_path -o $metallib_path --output-reflection-file=$reflection_path
else
	spirv_path=$filepath.spirv
	dxc $1 -Fo $spirv_path -T $shader_model -E $entry_point -spirv
fi

# rm $dxil_path
