set(target_name metalrender)

add_executable(${target_name}
    DebugSceneAuthoringLayer.cpp
    main.cpp
)

target_link_libraries(${target_name} PRIVATE teng_runtime teng_scene_authoring)
