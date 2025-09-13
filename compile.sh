#!/bin/bash
echo "Compiling shaders"
xcrun -sdk macosx metal -o basic1.ir -c resources/shaders/*.metal
xcrun -sdk macosx metal-ar -q basic1.air basic1.ir
xcrun -sdk macosx metallib -o default.metallib basic1.air
mkdir $1
cp default.metallib $1/default.metallib
rm default.metallib *.ir *.air
echo "Done compiling shaders"
