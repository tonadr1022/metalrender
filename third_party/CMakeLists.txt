add_subdirectory(metal-cpp)
add_subdirectory(glfw)
add_subdirectory(glm)

add_library(lib_imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_tables.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_metal.mm)
set_source_files_properties(
    imgui/backends/imgui_impl_metal.mm
    PROPERTIES
    COMPILE_DEFINITIONS "IMGUI_IMPL_METAL_CPP"
)

target_link_libraries(lib_imgui PUBLIC METAL_CPP glfw)

add_subdirectory(OffsetAllocator)
#add_library(offset_allocator
#    OffsetAllocator/offsetAllocator.cpp
#)
#target_include_directories(offset_allocator PUBLIC
#    OffsetAllocator
#)

target_include_directories(lib_imgui PUBLIC
    imgui
    imgui/misc/cpp
    imgui/backends)

set(MESHOPT_INSTALL OFF)
add_subdirectory(meshoptimizer)

option(TRACY_ENABLE "Enable profiling" ON)
option(TRACY_ONLY_IPV4 "IPv4 only" ON)

if (MSVC)
    add_compile_options(/wd4996)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
endif()

add_subdirectory(tracy)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(CMAKE_CXX_FLAGS "${_saved_cxx_flags}")
endif()

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cgltf/include)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb_image/include)
