# add_library(METAL_CPP ${CMAKE_CURRENT_SOURCE_DIR}/definition.cpp)
add_library(METAL_CPP INTERFACE)

target_include_directories(METAL_CPP SYSTEM INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/metal-cpp"
)

target_link_libraries(METAL_CPP INTERFACE
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore"
)
