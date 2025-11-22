# Another Renderer

I've been working on this renderer to enlighten myself on the wonders
of the Metal API while I don't have access to a desktop.

The pre_rhi branch contains meshlet occlusion culling and primitive voxel rendering. The main branch is an ongoing attempt at a render hardware interface (RHI) wrapping Metal and eventually Vulkan to enable a single renderer path to run on multiple platforms.

Initially written in Metal 3, I switched to Metal 4 for the first implementation of the RHI, recently realizing it's practically unusable without GPU debugging tools. I'm now supporting both Metal 3 and 4, since working without a GPU debugger/profiler is mega cursed.

Here's some Suzanne meshlets:

![Suzanne Meshlets](./screenshots/suzanne_meshlets.png "Suzanne_Meshlets")

## Running locally

```bash
git clone https://github.com/tonadr1022/metalrender
cd metalrender
git submodule update --init --recursive
cmake --preset Release
cmake --build build/Release
# Download glTF Sample Assets: https://github.com/KhronosGroup/glTF-Sample-Assets
./scripts/download_gltf_models.sh $HOME/gltf_sample_assets
ln -s $HOME/gltf_sample_assets/models/gltf
./build/Release/src/metalrender
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
