cmake_minimum_required(VERSION 3.24)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(metalrender VERSION 0.1 LANGUAGES C CXX OBJC OBJCXX)

if (DEFINED APPLE)
    set(BUILD_METAL ON)
else()
    set(BUILD_METAL OFF)
endif()

option(BUILD_VULKAN "Build Vulkan" OFF)
if (BUILD_VULKAN)
    set(BUILD_VULKAN ON)
endif()


set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_SOURCE_DIR}/cmake")

include(Warnings)
add_project_warnings(project_warnings)

add_subdirectory(third_party)
add_subdirectory(src)
add_subdirectory(app)
