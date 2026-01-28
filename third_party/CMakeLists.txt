set(CMAKE_CXX_STANDARD 23)

if (BUILD_METAL)
    add_subdirectory(metal-cpp)
    add_subdirectory(metal_irconverter_runtime)
endif()

if (BUILD_VULKAN)

    find_package(Vulkan REQUIRED)
    message(STATUS "Vulkan found: ${Vulkan_FOUND}")
    message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
    message(STATUS "Vulkan libraries: ${Vulkan_LIBRARIES}")

    set(VMA_STATIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)
    set(VMA_DYNAMIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)

    add_subdirectory(VulkanMemoryAllocator)
    add_subdirectory(volk)
    add_subdirectory(vk-bootstrap)
endif()

set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_VK_UPLOAD OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_STATIC_LIBRARY OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_LOADTEST_APPS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
set(KTX_FEATURE_DOC OFF CACHE BOOL "" FORCE)
set(BASISU_SUPPORT_SSE OFF CACHE BOOL "" FORCE)
add_subdirectory(ktx)

add_subdirectory(glfw)
add_subdirectory(glm)
target_compile_definitions(glm INTERFACE VK_NO_PROTOTYPES)

add_subdirectory(concurrentqueue)

set(FASTNOISE2_NOISETOOL OFF CACHE BOOL "Build Noise Tool" FORCE)
add_subdirectory(FastNoise2)

add_library(bs_thread_pool INTERFACE)
target_include_directories(bs_thread_pool INTERFACE thread-pool/include)

add_library(lib_imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_tables.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
    imgui/backends/imgui_impl_glfw.cpp
)

set(IMPLOT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/implot)
set(IMPLOT_SOURCE
    ${IMPLOT_SOURCE_DIR}/implot.cpp
    ${IMPLOT_SOURCE_DIR}/implot_demo.cpp
    ${IMPLOT_SOURCE_DIR}/implot_items.cpp
)
add_library(lib_implot STATIC ${IMPLOT_SOURCE})
target_include_directories(lib_implot PUBLIC ${IMPLOT_SOURCE_DIR})
target_link_libraries(lib_implot PUBLIC lib_imgui)

if (BUILD_VULKAN)
    target_sources(lib_imgui PRIVATE
        imgui/backends/imgui_impl_vulkan.cpp
    )
    target_include_directories(lib_imgui PUBLIC
        ${Vulkan_INCLUDE_DIRS}
    )

    target_compile_definitions(lib_imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
    target_link_libraries(lib_imgui PRIVATE volk::volk)
endif()

target_link_libraries(lib_imgui PUBLIC glfw )


add_subdirectory(OffsetAllocator)

target_include_directories(lib_imgui PUBLIC
    imgui
    imgui/misc/cpp
    imgui/backends)

set(MESHOPT_INSTALL OFF)
add_subdirectory(meshoptimizer)

option(TRACY_STATIC "Build Tracy as a static library" ON)
option(TRACY_ENABLE "Enable Tracy profiler" ON)
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
