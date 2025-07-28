
set(target_name metalrender)

add_executable(${target_name}
main.cpp
gfx/MetalCpp.cpp
gfx/DeviceMetal.cpp
WindowApple.cpp
)

set_source_files_properties(WindowApple.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${target_name} PRIVATE METAL_CPP glfw)

target_compile_options(${target_name}
    PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
    -Wall
    -Wextra
    -pedantic-errors
    -Wno-missing-field-initializers
    -Wno-unused-result
    >
    $<$<CXX_COMPILER_ID:MSVC>:
    /W4
    /WX
    /permissive-
    >
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Werror)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${target_name} PRIVATE /W4 /WX)
    endif()
endif()


