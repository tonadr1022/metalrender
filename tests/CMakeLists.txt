add_library(teng_engine_smoke STATIC
    smoke/AssetServiceSmokeTest.cpp
    smoke/AssetRegistrySmokeTest.cpp
    smoke/AssetDatabaseSmokeTest.cpp
    smoke/GeneratedSceneAssetsSmokeTest.cpp
    smoke/SceneSerializationSmokeTest.cpp
    smoke/SceneSmokeTest.cpp
)
target_link_libraries(teng_engine_smoke PUBLIC teng_runtime)
target_include_directories(teng_engine_smoke
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke
)

add_executable(teng_core_tests
    core/DiagnosticTests.cpp
)
target_link_libraries(teng_core_tests PRIVATE teng_core Catch2::Catch2WithMain project_warnings)
