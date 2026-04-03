#!/bin/bash

if [ -z "$1" ]; then
	echo "usage: download_gltf_models.sh <download_dir>"
	exit 1
fi

if [ -d "$1"/Models/Sponza ]; then
	echo "Sponza already exists, skipping download"
else
	git clone git@github.com:KhronosGroup/glTF-Sample-Assets.git $1
fi

rm -rf ./resources/models/gltf
ln -s $1 ./resources/models/gltf
