# Scene foundation/extraction/FPS smokes stay in libteng: they call Flecs through headers while
# flecs::flecs_static is linked privately into libteng, so those symbols are not available to a
# separate static smoke library without exporting Flecs from the DSO or duplicating the runtime.

set(COMMON_DIR ${CMAKE_SOURCE_DIR}/apps/common)
set(VKTEST_DIR ${CMAKE_SOURCE_DIR}/apps/vktest)
set(METALRENDER_APP_DIR ${CMAKE_SOURCE_DIR}/apps/metalrender)

add_library(teng_engine_smoke STATIC
    smoke/AssetServiceSmokeTest.cpp
    smoke/AssetRegistrySmokeTest.cpp
    smoke/AssetDatabaseSmokeTest.cpp
    smoke/DemoSceneAuthoringSmokeTest.cpp
)
target_link_libraries(teng_engine_smoke PUBLIC teng)
target_include_directories(teng_engine_smoke
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke
    PRIVATE
        ${COMMON_DIR}
        ${VKTEST_DIR}
        ${METALRENDER_APP_DIR}
)
