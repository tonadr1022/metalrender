set(target_name metalrender)

add_executable(${target_name}
    main.cpp
)

target_link_libraries(${target_name} PUBLIC teng)
