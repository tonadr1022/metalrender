set(target_name vktest)

set(COMMON_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)
set(METALRENDER_APP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../metalrender)

add_executable(${target_name}
    main.cpp
    ${COMMON_SOURCE_DIR}/Util.cpp
    ${METALRENDER_APP_DIR}/Camera.cpp
    ${METALRENDER_APP_DIR}/FpsCameraController.cpp
    TestDebugScenes.cpp
    TestRenderer.cpp
    TestApp.cpp
    MeshletRendererTestScene.cpp
)

target_include_directories(${target_name} PRIVATE ${COMMON_SOURCE_DIR} ${METALRENDER_APP_DIR})
target_link_libraries(${target_name} PUBLIC teng)
