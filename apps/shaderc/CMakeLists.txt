set(target_name teng-shaderc)

add_executable(${target_name} main.cpp)
target_link_libraries(${target_name} PRIVATE teng_shader_compiler project_warnings cxxopts::cxxopts)
