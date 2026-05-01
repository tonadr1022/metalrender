add_library(teng_engine_smoke STATIC
    smoke/AssetServiceSmokeTest.cpp
    smoke/AssetRegistrySmokeTest.cpp
    smoke/AssetDatabaseSmokeTest.cpp
    smoke/GeneratedSceneAssetsSmokeTest.cpp
    smoke/SceneAssetLoaderSmokeTest.cpp
    smoke/SceneSmokeTest.cpp
)
target_link_libraries(teng_engine_smoke PUBLIC teng_runtime)
target_include_directories(teng_engine_smoke
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke
)
