#!/bin/bash

if [ -z "$1" ]; then
	echo "usage: download_gltf_models.sh <download_dir>"
fi

git clone git@github.com:KhronosGroup/glTF-Sample-Assets.git $1
ln -s $1 ./resources/models/gltf
