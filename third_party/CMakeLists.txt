add_subdirectory(metal-cpp)
add_subdirectory(glfw)
add_subdirectory(glm)

add_library(lib_imgui
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_tables.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_metal.mm)

target_link_libraries(lib_imgui PUBLIC METAL_CPP glfw)

target_include_directories(lib_imgui PUBLIC
    imgui
    imgui/misc/cpp
    imgui/backends)

set(MESHOPT_INSTALL OFF)
add_subdirectory(meshoptimizer)

option(TRACY_ENABLE "Enable profiling" ON)
option(TRACY_ONLY_IPV4 "IPv4 only" ON)
add_subdirectory(tracy)

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cgltf/include)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb_image/include)
