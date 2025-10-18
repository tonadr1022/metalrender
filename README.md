# Metal Renderer (maybe minecraft 2)

I've been working on this renderer to enlighten myself on the wonders
of the Metal API while I don't have access to a desktop.
So far, it's been focused on meshlet rendering, with the possibility to expand it into Minecraft 2 (LOL).

![Suzanne Meshlets](./screenshots/suzanne_meshlets.png "Suzanne_Meshlets")

## Running locally

```bash
git clone https://github.com/tonadr1022/metalrender
cd metalrender
git submodule update --init --recursive
cmake --preset Release
cmake --build build/Release
# Download glTF Sample Assets: https://github.com/KhronosGroup/glTF-Sample-Assets
./download_gltf_models.sh $HOME/gltf_sample_assets
./build/bin/Release/metalrender
```

## Current Features

- meshlets
- meshlet occlusion, frustum, cone culling
- dynamic model loading

## References

- [Mesh Shaders and Meshlet Culling in Metal 3](https://metalbyexample.com/mesh-shaders/)
- [Zeux's Niagara Renderer](https://github.com/zeux/niagara)
- [Metal Shading Language](https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf)

## Third Party Libraries

- [cgltf](https://github.com/jkuhlmann/cgltf)
- [glfw](https://github.com/glfw/glfw)
- [glm](https://github.com/g-truc/glm)
- [imgui](https://github.com/ocornut/imgui)
- [meshoptimizer](https://github.com/zeux/meshoptimizer)
- [metal-cpp](https://developer.apple.com/metal/cpp/)
- [stb_image](https://github.com/nothings/stb)
- [tracy](https://github.com:wolfpld/tracy)
- [OffsetAllocator](https://github.com/sebbbi/OffsetAllocator)

## TODO

- reserve sizes in model load
