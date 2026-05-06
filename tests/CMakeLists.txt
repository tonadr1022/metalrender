add_library(teng_engine_smoke STATIC
    smoke/AssetServiceSmokeTest.cpp
    smoke/AssetRegistrySmokeTest.cpp
    smoke/AssetDatabaseSmokeTest.cpp
    smoke/GeneratedSceneAssetsSmokeTest.cpp
    smoke/SceneSerializationSmokeTest.cpp
    smoke/SceneSmokeTest.cpp
    smoke/TestHelpers.cpp
)
target_link_libraries(teng_engine_smoke PUBLIC teng_runtime)
target_include_directories(teng_engine_smoke
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke
)

add_executable(teng_core_tests
    core/ComponentRegistryTests.cpp
    core/DiagnosticTests.cpp
)
target_link_libraries(teng_core_tests PRIVATE teng_core teng_scene Catch2::Catch2WithMain project_warnings)

add_executable(teng_engine_tests
    smoke/SceneComponentContextTests.cpp
    smoke/SceneSerializationValidationTests.cpp
)
target_link_libraries(teng_engine_tests PRIVATE teng_engine_smoke Catch2::Catch2WithMain project_warnings)

if (BUILD_TESTING)
    include(${PROJECT_SOURCE_DIR}/third_party/Catch2/extras/Catch.cmake)
    catch_discover_tests(teng_core_tests
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        PROPERTIES
            LABELS unit
    )
    catch_discover_tests(teng_engine_tests
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        PROPERTIES
            LABELS unit
    )
endif()
