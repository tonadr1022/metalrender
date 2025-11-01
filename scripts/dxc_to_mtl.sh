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

entry_point=$type\_main

full_path=$1
filename="${full_path##*/}"
basename="${filename%.*}"

if [[ ! -d "resources/shader_out/metal" ]]; then
	mkdir resources/shader_out/metal
fi

dxil_path=/tmp/$basename\_$type.dxil
metallib_path=resources/shader_out/metal/$basename\_$type.metallib

echo "Compiling to $metallib_path"

dxc $1 -Fo $dxil_path -T $shader_model -E $entry_point
metal-shaderconverter $dxil_path -o $metallib_path
rm $dxil_path
