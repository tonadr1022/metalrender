set(target_name teng)

add_library(teng_shader_compiler STATIC
    shader_compiler/ShaderCompiler.cpp
)
target_include_directories(teng_shader_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(teng_shader_compiler PUBLIC project_warnings)

set(TENG_CORE_SOURCES
    core/FileUtil.cpp
    core/Util.cpp
    core/StringUtil.cpp
    core/CVar.cpp
    core/Console.cpp
    util/Stats.cpp
    util/FuzzyMatch.cpp
)

set(TENG_PLATFORM_SOURCES
    Window.cpp
    UI.cpp
)

set(TENG_ENGINE_SOURCES
    engine/Engine.cpp
    engine/ImGuiOverlayLayer.cpp
    engine/assets/AssetDatabase.cpp
    engine/assets/AssetRegistry.cpp
    engine/assets/AssetService.cpp
    engine/render/DebugClearRenderer.cpp
    engine/render/RenderSceneExtractor.cpp
    engine/render/RenderService.cpp
    engine/scene/Scene.cpp
    engine/scene/SceneComponents.cpp
    engine/scene/SceneIds.cpp
    engine/scene/SceneManager.cpp
)

set(TENG_GFX_SOURCES
    ResourceManager.cpp
    gfx/ModelLoader.cpp
    gfx/ModelInstance.cpp
    gfx/ShaderManager.cpp
    gfx/ImGuiRenderer.cpp
    gfx/GPUFrameAllocator.cpp
    gfx/GPUFrameAllocator2.cpp
    gfx/DrawBatch.cpp
    gfx/RenderGraph.cpp
    gfx/RenderGraph.Bake.cpp
    gfx/RenderGraph.Format.cpp
    gfx/RenderGraph.DebugDump.cpp
    gfx/BackedGPUAllocator.cpp
    gfx/ModelGPUManager.cpp
    gfx/renderer/BufferResize.cpp
    gfx/renderer/InstanceMgr.cpp
    gfx/renderer/RendererCVars.cpp
    gfx/renderer/ModelGPUUploader.cpp
    gfx/renderer/MeshletCsmRenderer.cpp
    gfx/renderer/MeshletDepthPyramid.cpp
    gfx/renderer/MeshletDrawPrep.cpp
    gfx/renderer/MeshletTestRenderUtil.cpp
    gfx/renderer/MeshletRenderer.cpp
    gfx/texture/KtxLoad.cpp
    gfx/rhi/Pipeline.cpp
    gfx/rhi/Texture.cpp
    gfx/rhi/Device.cpp
)

set(TENG_VULKAN_SOURCES
    gfx/vulkan/VulkanDevice.cpp
    gfx/vulkan/VulkanCommon.cpp
    gfx/vulkan/VulkanLibs.cpp
    gfx/vulkan/VulkanCmdEncoder.cpp
    gfx/vulkan/VkUtil.cpp
    gfx/vulkan/VulkanDeleteQueue.cpp
)

set(TENG_METAL_PLATFORM_SOURCES
    platform/apple/AppleWindow.mm
)

set(TENG_METAL_GFX_SOURCES
    gfx/metal/MetalCpp.cpp
    gfx/metal/MetalCmdEncoderICBMgr.cpp
    gfx/metal/MetalDevice.cpp
    gfx/metal/MetalBuffer.cpp
    gfx/metal/MetalUtil.cpp
    gfx/metal/MetalCmdEncoder.cpp
    gfx/metal/MetalWindow.cpp
    gfx/metal/MetalIRRuntime.cpp
)

if(BUILD_METAL)
    set_source_files_properties(gfx/metal/MetalDevice.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")
    set_source_files_properties(gfx/metal/MetalWindow.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")

    add_custom_target(compile_metal_backend_shaders ALL
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/compile_custom_mtl_shaders.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

set(TENG_PUBLIC_LIBS
    teng_shader_compiler
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
    fts_fuzzy_match
    tomlplusplus
)

function(configure_teng_compile_target target_name build_shared)
    target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/resources/shaders)
    target_include_directories(${target_name} PUBLIC ${CMAKE_SOURCE_DIR}/third_party/vendored)
    target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(${target_name} PUBLIC ${CMAKE_SOURCE_DIR}/third_party/flecs/include)

    target_compile_definitions(${target_name} PUBLIC TENG_BUILD_SHARED=${build_shared})
    target_link_libraries(${target_name} PUBLIC ${TENG_PUBLIC_LIBS})
endfunction()

function(configure_teng_component target_name build_shared)
    configure_teng_compile_target(${target_name} ${build_shared})

    if(BUILD_VULKAN)
        target_compile_definitions(${target_name} PUBLIC VULKAN_BACKEND=1)
        target_link_libraries(${target_name} PRIVATE
            volk::volk
            GPUOpen::VulkanMemoryAllocator
            vk-bootstrap::vk-bootstrap
            spirv-reflect-static
        )
    endif()

    if(BUILD_METAL)
        target_compile_definitions(${target_name} PRIVATE METAL_BACKEND=1)
        target_link_libraries(${target_name} PRIVATE
            metal_irconverter_runtime
            METAL_CPP
        )
    endif()
endfunction()

function(add_teng_component component_name build_shared)
    set(target_name ${component_name})
    if(build_shared)
        set(target_name ${component_name}_shared)
    else()
        set(target_name ${component_name}_static)
    endif()

    add_library(${target_name} OBJECT ${ARGN})
    configure_teng_component(${target_name} ${build_shared})

    if(BUILD_METAL)
        add_dependencies(${target_name} compile_metal_backend_shaders)
    endif()
endfunction()

function(configure_teng_aggregate target_name build_shared)
    configure_teng_compile_target(${target_name} ${build_shared})

    target_link_libraries(${target_name} PRIVATE flecs::flecs_static)

    if(BUILD_VULKAN)
        target_compile_definitions(${target_name} PUBLIC VULKAN_BACKEND=1)
        target_link_libraries(${target_name} PRIVATE
            volk::volk
            GPUOpen::VulkanMemoryAllocator
            vk-bootstrap::vk-bootstrap
            spirv-reflect-static
        )
    endif()

    if(BUILD_METAL)
        target_compile_definitions(${target_name} PRIVATE METAL_BACKEND=1)
        target_link_libraries(${target_name} PRIVATE
            metal_irconverter_runtime
            METAL_CPP
        )
        add_dependencies(${target_name} compile_metal_backend_shaders)
    endif()
endfunction()

if(BUILD_VULKAN)
    list(APPEND TENG_GFX_SOURCES ${TENG_VULKAN_SOURCES})
endif()

if(BUILD_METAL)
    list(APPEND TENG_PLATFORM_SOURCES ${TENG_METAL_PLATFORM_SOURCES})
    list(APPEND TENG_GFX_SOURCES ${TENG_METAL_GFX_SOURCES})
endif()

add_teng_component(teng_core 1 ${TENG_CORE_SOURCES})
add_teng_component(teng_platform 1 ${TENG_PLATFORM_SOURCES})
add_teng_component(teng_gfx 1 ${TENG_GFX_SOURCES})
add_teng_component(teng_engine 1 ${TENG_ENGINE_SOURCES})

add_teng_component(teng_core 0 ${TENG_CORE_SOURCES})
add_teng_component(teng_platform 0 ${TENG_PLATFORM_SOURCES})
add_teng_component(teng_gfx 0 ${TENG_GFX_SOURCES})
add_teng_component(teng_engine 0 ${TENG_ENGINE_SOURCES})

target_link_libraries(teng_platform_shared PUBLIC teng_core_shared)
target_link_libraries(teng_gfx_shared PUBLIC teng_core_shared teng_platform_shared)
target_link_libraries(teng_engine_shared PUBLIC teng_core_shared teng_platform_shared teng_gfx_shared)
target_link_libraries(teng_engine_shared PRIVATE flecs::flecs_static)

target_link_libraries(teng_platform_static PUBLIC teng_core_static)
target_link_libraries(teng_gfx_static PUBLIC teng_core_static teng_platform_static)
target_link_libraries(teng_engine_static PUBLIC teng_core_static teng_platform_static teng_gfx_static)
target_link_libraries(teng_engine_static PRIVATE flecs::flecs_static)

add_library(${target_name} SHARED
    $<TARGET_OBJECTS:teng_core_shared>
    $<TARGET_OBJECTS:teng_platform_shared>
    $<TARGET_OBJECTS:teng_gfx_shared>
    $<TARGET_OBJECTS:teng_engine_shared>
)
configure_teng_aggregate(${target_name} 1)

add_library(teng_static STATIC
    $<TARGET_OBJECTS:teng_core_static>
    $<TARGET_OBJECTS:teng_platform_static>
    $<TARGET_OBJECTS:teng_gfx_static>
    $<TARGET_OBJECTS:teng_engine_static>
)
configure_teng_aggregate(teng_static 0)
