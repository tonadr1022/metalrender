set(target_name engine_scene_smoke)

add_executable(${target_name} main.cpp)
target_link_libraries(${target_name} PRIVATE teng)
