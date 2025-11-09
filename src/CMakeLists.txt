set(target_name metalrender)

add_executable(${target_name}
    main.cpp
    App.cpp
    Camera.cpp
    core/FileUtil.cpp
    core/Util.cpp
    gfx/metal/MetalCpp.cpp
    gfx/metal/MetalDevice.cpp
    gfx/metal/MetalBuffer.cpp
    gfx/metal/MetalUtil.cpp
    gfx/metal/MetalCmdEncoder.cpp
    gfx/ModelLoader.cpp
    gfx/RendererMetal.cpp
    gfx/RendererMetal4.cpp
    gfx/Pipeline.cpp
    gfx/ModelInstance.cpp
    gfx/ResourceManager.cpp
    gfx/GPUFrameAllocator.cpp
    gfx/RenderGraph.cpp
    WindowApple.cpp
    voxels/Chunk.cpp
    voxels/Mesher.cpp
    voxels/TerrainGenerator.cpp
    voxels/VoxelRenderer.cpp
    voxels/VoxelDB.cpp
    voxels/VoxelWorld.cpp
    util/Stats.cpp
)

add_custom_target(compile_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Compile shaders"
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/compile_hlsl.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_dependencies(${target_name} compile_shaders)

set_source_files_properties(gfx/Metal/MetalDevice.cpp WindowApple.cpp  PROPERTIES COMPILE_FLAGS "-x objective-c++")
target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/resources/shaders)
target_link_libraries(${target_name} PRIVATE
    glm::glm
    project_warnings
    cgltf
    stb_image
    meshoptimizer
    Tracy::TracyClient
    lib_imgui
    offsetAllocator
    FastNoise
    bs_thread_pool
    concurrentqueue
    metal_irconverter_runtime
)
