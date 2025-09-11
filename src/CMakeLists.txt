
set(target_name metalrender)

add_executable(${target_name}
    main.cpp
    App.cpp
    core/FileUtil.cpp
    gfx/metal/MetalCpp.cpp
    gfx/metal/MetalDevice.cpp
    gfx/metal/MetalBuffer.cpp
    gfx/metal/MetalUtil.cpp
    gfx/ModelLoader.cpp
    gfx/RendererMetal.cpp
    WindowApple.cpp
    voxels/Chunk.hpp
    voxels/TerrainGenerator.hpp
    voxels/VoxelRenderer.cpp
)

set_source_files_properties(WindowApple.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${target_name} PRIVATE METAL_CPP glfw glm::glm project_warnings cgltf stb_image)
