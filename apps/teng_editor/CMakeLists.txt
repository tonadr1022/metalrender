set(target_name teng_editor)

add_executable(${target_name}
    main.cpp
)

target_link_libraries(${target_name} PRIVATE teng_runtime lib_teng_editor)
