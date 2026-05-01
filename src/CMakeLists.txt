add_library(teng_shader_compiler STATIC
    shader_compiler/ShaderCompiler.cpp
)
target_include_directories(teng_shader_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(teng_shader_compiler PUBLIC project_warnings)

set(TENG_CORE_SOURCES
    core/FileUtil.cpp
    core/Util.cpp
    core/StringUtil.cpp
    util/Stats.cpp
    util/FuzzyMatch.cpp
)

set(TENG_CVAR_SOURCES
    core/CVar.cpp
    core/Console.cpp
)

set(TENG_PLATFORM_SOURCES
    Window.cpp
    UI.cpp
)

set(TENG_ASSETS_SOURCES
    engine/assets/AssetDatabase.cpp
    engine/assets/AssetRegistry.cpp
)

set(TENG_SCENE_SOURCES
    engine/scene/Scene.cpp
    engine/scene/SceneAssetLoader.cpp
    engine/scene/SceneComponents.cpp
    engine/scene/SceneIds.cpp
    engine/scene/SceneManager.cpp
)

set(TENG_RENDER_SOURCES
    engine/render/DebugClearRenderer.cpp
    engine/render/RenderSceneExtractor.cpp
    engine/render/RenderService.cpp
)

set(TENG_ENGINE_RUNTIME_SOURCES
    engine/Engine.cpp
    engine/ImGuiOverlayLayer.cpp
    engine/assets/AssetService.cpp
)

set(TENG_GFX_SOURCES
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

set(TENG_COMMON_PUBLIC_LIBS
    glm::glm
    project_warnings
    bs_thread_pool
    tomlplusplus
)

set(TENG_BACKEND_LIBS)
if(BUILD_VULKAN)
    list(APPEND TENG_BACKEND_LIBS
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        vk-bootstrap::vk-bootstrap
        spirv-reflect-static
    )
endif()

if(BUILD_METAL)
    list(APPEND TENG_BACKEND_LIBS
        metal_irconverter_runtime
        METAL_CPP
    )
endif()

function(configure_teng_compile_target target_name)
    target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/resources/shaders)
    target_include_directories(${target_name} PUBLIC ${CMAKE_SOURCE_DIR}/third_party/vendored)
    target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(${target_name} PUBLIC ${CMAKE_SOURCE_DIR}/third_party/flecs/include)

    target_link_libraries(${target_name} PUBLIC ${TENG_COMMON_PUBLIC_LIBS})
endfunction()

function(configure_teng_component target_name uses_backend)
    configure_teng_compile_target(${target_name})

    if(uses_backend AND BUILD_VULKAN)
        target_compile_definitions(${target_name} PUBLIC VULKAN_BACKEND=1)
        target_link_libraries(${target_name} PRIVATE ${TENG_BACKEND_LIBS})
    endif()

    if(uses_backend AND BUILD_METAL)
        target_compile_definitions(${target_name} PRIVATE METAL_BACKEND=1)
        target_link_libraries(${target_name} PRIVATE ${TENG_BACKEND_LIBS})
    endif()
endfunction()

function(add_teng_component component_name uses_backend)
    add_library(${component_name} STATIC ${ARGN})
    configure_teng_component(${component_name} ${uses_backend})

    if(uses_backend AND BUILD_METAL)
        add_dependencies(${component_name} compile_metal_backend_shaders)
    endif()
endfunction()

function(teng_link_flecs target_name)
    target_link_libraries(${target_name} PRIVATE flecs::flecs_static)
endfunction()

if(BUILD_VULKAN)
    list(APPEND TENG_GFX_SOURCES ${TENG_VULKAN_SOURCES})
endif()

if(BUILD_METAL)
    list(APPEND TENG_PLATFORM_SOURCES ${TENG_METAL_PLATFORM_SOURCES})
    list(APPEND TENG_GFX_SOURCES ${TENG_METAL_GFX_SOURCES})
endif()

add_teng_component(teng_core OFF ${TENG_CORE_SOURCES})
add_teng_component(teng_cvars OFF ${TENG_CVAR_SOURCES})
add_teng_component(teng_platform OFF ${TENG_PLATFORM_SOURCES})
add_teng_component(teng_gfx ON ${TENG_GFX_SOURCES})
add_teng_component(teng_assets OFF ${TENG_ASSETS_SOURCES})
add_teng_component(teng_scene OFF ${TENG_SCENE_SOURCES})
add_teng_component(teng_scene_validate OFF engine/scene/SceneValidate.cpp)
add_teng_component(teng_render ON ${TENG_RENDER_SOURCES})
add_teng_component(teng_engine_runtime ON ${TENG_ENGINE_RUNTIME_SOURCES})
add_teng_component(teng_runtime OFF engine/RuntimeLink.cpp)

target_link_libraries(teng_core PRIVATE fts_fuzzy_match)
target_link_libraries(teng_cvars PUBLIC teng_core PRIVATE implot Tracy::TracyClient)
target_link_libraries(teng_platform PUBLIC teng_core glfw PRIVATE imgui Tracy::TracyClient)
target_link_libraries(teng_gfx
    PUBLIC teng_core teng_cvars teng_platform imgui offsetAllocator
    PRIVATE teng_shader_compiler cgltf meshoptimizer ktx Tracy::TracyClient
)
target_link_libraries(teng_assets PUBLIC teng_core)
target_link_libraries(teng_scene PUBLIC teng_core teng_assets)
teng_link_flecs(teng_scene)
target_link_libraries(teng_scene_validate PUBLIC teng_core teng_assets teng_scene)
target_link_libraries(teng_render PUBLIC teng_core teng_scene teng_gfx PRIVATE Tracy::TracyClient)
target_link_libraries(teng_engine_runtime PUBLIC
    teng_core
    teng_cvars
    teng_platform
    teng_gfx
    teng_assets
    teng_scene
    teng_render
)
target_link_libraries(teng_engine_runtime PRIVATE implot Tracy::TracyClient)
add_dependencies(teng_runtime teng_engine_runtime)
target_link_libraries(teng_runtime INTERFACE teng_engine_runtime Tracy::TracyClient)
