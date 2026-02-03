set(target_name vktest)

set(COMMON_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

add_executable(${target_name}
    main.cpp
    ${COMMON_SOURCE_DIR}/Util.cpp
    TestRenderer.cpp
    TestApp.cpp
)

target_include_directories(${target_name} PRIVATE ${COMMON_SOURCE_DIR})
target_link_libraries(${target_name} PUBLIC teng)
