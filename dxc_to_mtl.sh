#!/bin/bash

if [[ "$2" == *"vert"* ]]; then
	shader_model="vs_6_7"
elif [[ "$2" == *"frag"* ]]; then
	shader_model="ps_6_7"
else
	echo "unable to determine shader model from dxc output filename"
fi

/Users/tony/clone/DirectXShaderCompiler/build/bin/dxc $1 -Fo $2.dxil -T $shader_model -E $3
metal-shaderconverter $2.dxil -o $2.metallib --output-reflection-file=$2\_reflection.json
mv *.metallib resources/shader_out
# /Users/tony/clone/DirectXShaderCompiler/build/bin/dxc $1 -Fo $2 -T vs_6_9 -E $3
