set(COMMON_DIR ${CMAKE_SOURCE_DIR}/apps/common)
set(VKTEST_DIR ${CMAKE_SOURCE_DIR}/apps/vktest)
set(METALRENDER_APP_DIR ${CMAKE_SOURCE_DIR}/apps/metalrender)

add_library(teng_engine_smoke STATIC
    smoke/AssetServiceSmokeTest.cpp
    smoke/AssetRegistrySmokeTest.cpp
    smoke/AssetDatabaseSmokeTest.cpp
    smoke/DemoSceneAuthoringSmokeTest.cpp
    smoke/SceneSmokeTest.cpp
)
target_link_libraries(teng_engine_smoke PUBLIC teng_static)
target_include_directories(teng_engine_smoke
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke
    PRIVATE
        ${COMMON_DIR}
        ${VKTEST_DIR}
        ${METALRENDER_APP_DIR}
)
