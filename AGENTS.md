# Agent Instructions

### Target Names
vulkan app target_name: vktest
metal app target_name: metalrender
### Configure
cmake --preset Debug
### Build
cmake --build build/Debug --target <target_name>
### Run
./build/Debug/bin/<target_name>
### Compile shaders (standalone)
target_name: `teng-shaderc`  
`./build/Debug/bin/teng-shaderc [--project-root <repo>] <path/to/file.comp.hlsl> [...]`  
If omitted, `--project-root` is inferred by walking up until `resources/shaders/hlsl` exists.