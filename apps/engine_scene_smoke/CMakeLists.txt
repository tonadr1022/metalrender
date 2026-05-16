set(target_name engine_scene_smoke)

add_executable(${target_name}
    main.cpp
)
target_link_libraries(${target_name} PRIVATE teng_engine_smoke cxxopts::cxxopts)

if (BUILD_TESTING)
    add_test(NAME ${target_name} COMMAND ${target_name})
    set_tests_properties(${target_name} PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        LABELS smoke
    )
endif()
