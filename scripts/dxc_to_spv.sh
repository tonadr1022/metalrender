#!/bin/bash

if [[ "$2" == *"vert"* ]]; then
	shader_model="vs_6_9"
elif [[ "$2" == *"frag"* ]]; then
	shader_model="ps_6_9"
else
	echo "unable to determine shader model from dxc output filename"
fi

dxc $1 -Fo $2 -T $shader_model -E $3 -spirv -fspv-target-env=vulkan1.3
