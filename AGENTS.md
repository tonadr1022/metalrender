# Agent Instructions

### Target Names
vulkan app <target_name>: vktest
metal app <target_name>: metalrender
### Configure
cmake --preset Debug
### Build
cmake --build build/Debug --target <target_name>
### Run
./build/Debug/bin/<target_name>