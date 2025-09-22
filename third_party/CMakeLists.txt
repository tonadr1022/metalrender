add_subdirectory(metal-cpp)
add_subdirectory(glfw)
add_subdirectory(glm)

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cgltf/include)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb_image/include)

set(MESHOPT_INSTALL OFF)
add_subdirectory(meshoptimizer)

option(TRACY_ENABLE "Enable profiling" ON)
option(TRACY_ONLY_IPV4 "IPv4 only" ON)
add_subdirectory(tracy)
