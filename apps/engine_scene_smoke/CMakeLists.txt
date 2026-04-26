set(target_name engine_scene_smoke)

set(COMMON_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)
set(METALRENDER_APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../metalrender)
set(VKTEST_APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vktest)

add_executable(${target_name}
    main.cpp
    ${COMMON_SOURCE_DIR}/ScenePresets.cpp
    ${METALRENDER_APP_DIR}/Camera.cpp
    ${VKTEST_APP_DIR}/DemoSceneEcsBridge.cpp
)
target_include_directories(${target_name} PRIVATE
    ${COMMON_SOURCE_DIR}
    ${METALRENDER_APP_DIR}
    ${VKTEST_APP_DIR}
)
target_link_libraries(${target_name} PRIVATE teng)
