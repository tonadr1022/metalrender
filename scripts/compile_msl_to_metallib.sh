#!/bin/bash

IN_PATH=$1
OUT_PATH=$2
AIR_PATH=/tmp/$(basename "$IN_PATH").air

xcrun -sdk macosx metal -c "$IN_PATH" -o "$AIR_PATH" -frecord-sources
xcrun -sdk macosx metallib "$AIR_PATH" -o "$OUT_PATH"
