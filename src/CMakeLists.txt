set(target_name teng)

add_library(${target_name} SHARED
    ResourceManager.cpp
    Window.cpp
    UI.cpp

    core/FileUtil.cpp
    core/Util.cpp
    core/StringUtil.cpp

    gfx/ModelLoader.cpp
    gfx/MemeRenderer123.cpp
    gfx/ModelInstance.cpp
    gfx/ShaderManager.cpp
    gfx/ImGuiRenderer.cpp
    gfx/GPUFrameAllocator.cpp
    gfx/GPUFrameAllocator2.cpp
    gfx/DrawBatch.cpp
    gfx/RenderGraph.cpp
    gfx/BackedGPUAllocator.cpp
    gfx/renderer/BufferResize.cpp

    gfx/texture/KtxLoad.cpp
    gfx/rhi/Pipeline.cpp
    gfx/rhi/Texture.cpp
    gfx/rhi/Device.cpp
    util/Stats.cpp

)




if(BUILD_VULKAN)
    target_sources(${target_name} PRIVATE
        gfx/vulkan/VulkanDevice.cpp
        gfx/vulkan/VulkanCommon.cpp
        gfx/vulkan/VulkanLibs.cpp
        gfx/vulkan/VulkanCmdEncoder.cpp
        gfx/vulkan/VkUtil.cpp
        gfx/vulkan/VulkanDeleteQueue.cpp
    )
    target_compile_definitions(${target_name} PUBLIC VULKAN_BACKEND=1)
endif()

if(BUILD_METAL)
    target_sources(${target_name} PRIVATE
        platform/apple/AppleWindow.mm
        gfx/metal/MetalCpp.cpp
        gfx/metal/MetalCmdEncoderICBMgr.cpp
        gfx/metal/MetalDevice.cpp
        gfx/metal/MetalBuffer.cpp
        gfx/metal/MetalUtil.cpp
        gfx/metal/MetalCmdEncoder.cpp
        gfx/metal/MetalWindow.cpp
        gfx/metal/MetalIRRuntime.cpp
    )

    target_compile_definitions(${target_name} PRIVATE METAL_BACKEND=1)
    set_source_files_properties(gfx/metal/MetalDevice.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
    set_source_files_properties(gfx/metal/MetalWindow.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")

    add_custom_target(compile_metal_backend_shaders ALL
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/compile_custom_mtl_shaders.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    add_dependencies(${target_name} compile_metal_backend_shaders)
endif()


target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/resources/shaders)
target_include_directories(${target_name} PUBLIC ${CMAKE_SOURCE_DIR}/third_party/vendored)
target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${target_name} PUBLIC
    glm::glm
    project_warnings
    cgltf
    meshoptimizer
    Tracy::TracyClient
    implot
    offsetAllocator
    FastNoise
    bs_thread_pool
    concurrentqueue
    ktx
)


if (BUILD_VULKAN)
    target_link_libraries(${target_name} PRIVATE
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        vk-bootstrap::vk-bootstrap
        spirv-reflect-static
    )
endif()

if (BUILD_METAL)
    target_link_libraries(${target_name} PRIVATE
        metal_irconverter_runtime
        METAL_CPP
    )
endif()
