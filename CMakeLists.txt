cmake_minimum_required(VERSION 3.24)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(projectwow VERSION 0.1 LANGUAGES C CXX OBJC OBJCXX)


add_subdirectory(third_party)
add_subdirectory(src)

