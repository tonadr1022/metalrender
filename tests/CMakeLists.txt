set(TENG_TEST_EXTENSION_REFLECT_OUT_DIR "${CMAKE_BINARY_DIR}/generated/teng_test_extension")
set(TENG_TEST_EXTENSION_REFLECT_GENERATED_HPP
    "${TENG_TEST_EXTENSION_REFLECT_OUT_DIR}/test_extension_reflect.generated.hpp"
)
set(TENG_TEST_EXTENSION_REFLECT_GENERATED_CPP
    "${TENG_TEST_EXTENSION_REFLECT_OUT_DIR}/test_extension_reflect.generated.cpp"
)
set(TENG_TEST_EXTENSION_REFLECT_MANIFEST
    "${TENG_TEST_EXTENSION_REFLECT_OUT_DIR}/test_extension_reflect.generated.manifest.json"
)

add_custom_command(
    OUTPUT
        "${TENG_TEST_EXTENSION_REFLECT_GENERATED_HPP}"
        "${TENG_TEST_EXTENSION_REFLECT_GENERATED_CPP}"
        "${TENG_TEST_EXTENSION_REFLECT_MANIFEST}"
    COMMAND
        $<TARGET_FILE:teng-component-codegen>
        --out-dir "${TENG_TEST_EXTENSION_REFLECT_OUT_DIR}"
        --module-name "test_extension"
        --output-basename "test_extension_reflect.generated"
        --namespace "teng::engine::test_extension_generated"
        --function-prefix "test_extension"
        --include "TestExtensionComponent.hpp"
        --typed-thunks
        --header "${CMAKE_CURRENT_SOURCE_DIR}/smoke/TestExtensionComponent.hpp"
        --extra-arg=-std=c++23
        --extra-arg=-I${CMAKE_SOURCE_DIR}/src
        --extra-arg=-I${CMAKE_SOURCE_DIR}/third_party/flecs/include
        --extra-arg=-I${CMAKE_SOURCE_DIR}/third_party/glm
        --extra-arg=-I${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/include
        ${TENG_COMPONENT_CODEGEN_SYSTEM_ARGS}
    DEPENDS
        teng-component-codegen
        "${CMAKE_CURRENT_SOURCE_DIR}/smoke/TestExtensionComponent.hpp"
    COMMENT "Generating test extension reflection code"
    VERBATIM
)

add_custom_target(
    teng_test_extension_reflect_codegen
    DEPENDS
        "${TENG_TEST_EXTENSION_REFLECT_GENERATED_HPP}"
        "${TENG_TEST_EXTENSION_REFLECT_GENERATED_CPP}"
        "${TENG_TEST_EXTENSION_REFLECT_MANIFEST}"
)

add_library(teng_test_extension_reflect_generated STATIC
    "${TENG_TEST_EXTENSION_REFLECT_GENERATED_CPP}"
)
add_dependencies(teng_test_extension_reflect_generated teng_test_extension_reflect_codegen)
target_include_directories(teng_test_extension_reflect_generated
    PUBLIC
        "${TENG_TEST_EXTENSION_REFLECT_OUT_DIR}"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/smoke"
)
target_link_libraries(teng_test_extension_reflect_generated PUBLIC teng_scene)

add_library(teng_engine_smoke STATIC
    smoke/AssetServiceSmokeTest.cpp
    smoke/AssetRegistrySmokeTest.cpp
    smoke/AssetDatabaseSmokeTest.cpp
    smoke/GeneratedSceneAssetsSmokeTest.cpp
    smoke/SceneSerializationSmokeTest.cpp
    smoke/SceneSmokeTest.cpp
    smoke/TestExtensionComponent.cpp
    smoke/TestHelpers.cpp
)
target_link_libraries(teng_engine_smoke PUBLIC teng_runtime teng_test_extension_reflect_generated)
target_include_directories(teng_engine_smoke
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/smoke
)

# -----------------------------------------------------------------------------
# Task 2: reflection codegen fixture path (build-dir only; generated C++ must compile).

set(TENG_REFLECT_FIXTURE_OUT_DIR "${CMAKE_BINARY_DIR}/generated/teng_reflect_fixtures")
set(TENG_REFLECT_FIXTURE_GENERATED_HPP
    "${TENG_REFLECT_FIXTURE_OUT_DIR}/fixtures_reflect.generated.hpp"
)
set(TENG_REFLECT_FIXTURE_GENERATED_CPP
    "${TENG_REFLECT_FIXTURE_OUT_DIR}/fixtures_reflect.generated.cpp"
)
set(TENG_REFLECT_FIXTURE_MANIFEST
    "${TENG_REFLECT_FIXTURE_OUT_DIR}/fixtures_reflect.manifest.json"
)

set(TENG_REFLECT_FIXTURE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/reflect_fixtures/ReflectScalarAndAssetFixture.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/reflect_fixtures/ReflectEnumAndPolicyFixture.hpp"
)

add_custom_command(
    OUTPUT
        "${TENG_REFLECT_FIXTURE_GENERATED_HPP}"
        "${TENG_REFLECT_FIXTURE_GENERATED_CPP}"
        "${TENG_REFLECT_FIXTURE_MANIFEST}"
    COMMAND
        $<TARGET_FILE:teng-component-codegen>
        --out-dir "${TENG_REFLECT_FIXTURE_OUT_DIR}"
        --module-name "fixtures"
        --output-basename "fixtures_reflect.generated"
        --manifest "${TENG_REFLECT_FIXTURE_MANIFEST}"
        --namespace "teng::engine::reflect_fixture_generated"
        --function-prefix "fixture"
        --include "ReflectScalarAndAssetFixture.hpp"
        --include "ReflectEnumAndPolicyFixture.hpp"
        --header "${CMAKE_CURRENT_SOURCE_DIR}/reflect_fixtures/ReflectScalarAndAssetFixture.hpp"
        --header "${CMAKE_CURRENT_SOURCE_DIR}/reflect_fixtures/ReflectEnumAndPolicyFixture.hpp"
        --typed-thunks
        --extra-arg=-std=c++23
        --extra-arg=-I${CMAKE_SOURCE_DIR}/src
        --extra-arg=-I${CMAKE_SOURCE_DIR}/third_party/flecs/include
        --extra-arg=-I${CMAKE_SOURCE_DIR}/third_party/glm
        --extra-arg=-I${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/include
        ${TENG_COMPONENT_CODEGEN_SYSTEM_ARGS}
    DEPENDS
        teng-component-codegen
        ${TENG_REFLECT_FIXTURE_HEADERS}
    COMMENT "Generating reflection fixture code"
    VERBATIM
)

add_custom_target(
    teng_reflect_fixture_codegen ALL
    DEPENDS
        "${TENG_REFLECT_FIXTURE_GENERATED_HPP}"
        "${TENG_REFLECT_FIXTURE_GENERATED_CPP}"
        "${TENG_REFLECT_FIXTURE_MANIFEST}"
)

add_library(teng_reflect_fixture_generated STATIC
    "${TENG_REFLECT_FIXTURE_GENERATED_CPP}"
)
add_dependencies(teng_reflect_fixture_generated teng_reflect_fixture_codegen)
target_include_directories(teng_reflect_fixture_generated PUBLIC "${TENG_REFLECT_FIXTURE_OUT_DIR}")
target_include_directories(teng_reflect_fixture_generated PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/reflect_fixtures"
)
target_link_libraries(teng_reflect_fixture_generated PUBLIC teng_scene)

target_link_libraries(teng_engine_smoke PUBLIC teng_reflect_fixture_generated)

add_executable(teng_core_tests
    core/ComponentRegistryTests.cpp
    core/DiagnosticTests.cpp
)
target_link_libraries(teng_core_tests PRIVATE teng_core teng_scene Catch2::Catch2WithMain project_warnings)

add_executable(teng_engine_tests
    smoke/SceneComponentContextTests.cpp
    smoke/SceneSerializationValidationTests.cpp
    smoke/TestModuleExtensionTests.cpp
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

    # Negative generator tests (invalid fixtures are not part of the build-integrated inputs).
    file(GLOB TENG_REFLECT_INVALID_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/reflect_fixtures/invalid/*.hpp"
    )
    foreach(invalid_header ${TENG_REFLECT_INVALID_HEADERS})
        get_filename_component(invalid_stem "${invalid_header}" NAME_WE)
        add_test(
            NAME "reflect_codegen_rejects_${invalid_stem}"
            COMMAND
                $<TARGET_FILE:teng-component-codegen>
                --out-dir "${CMAKE_BINARY_DIR}/generated/teng_reflect_invalid/${invalid_stem}"
                --module-name "invalid_${invalid_stem}"
                --header "${invalid_header}"
                --typed-thunks
                --extra-arg=-std=c++23
                --extra-arg=-I${CMAKE_SOURCE_DIR}/src
                --extra-arg=-I${CMAKE_SOURCE_DIR}/third_party/flecs/include
                --extra-arg=-I${CMAKE_SOURCE_DIR}/third_party/glm
                --extra-arg=-I${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/include
                ${TENG_COMPONENT_CODEGEN_SYSTEM_ARGS}
        )
        set_tests_properties("reflect_codegen_rejects_${invalid_stem}" PROPERTIES WILL_FAIL TRUE)
    endforeach()
endif()
