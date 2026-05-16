add_library(teng_shader_compiler STATIC
    shader_compiler/ShaderCompiler.cpp
)
target_include_directories(teng_shader_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(teng_shader_compiler PUBLIC project_warnings)

set(TENG_CORE_SOURCES
    core/Diagnostic.cpp
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

set(TENG_CONTENT_SOURCES
    engine/content/BinaryReader.cpp
    engine/content/BinaryWriter.cpp
    engine/content/CookedArtifact.cpp
)

set(TENG_CORE_COMPONENT_REFLECT_OUT_DIR "${CMAKE_BINARY_DIR}/generated/components/teng_core")
set(TENG_CORE_COMPONENT_REFLECT_GENERATED_HPP
    "${TENG_CORE_COMPONENT_REFLECT_OUT_DIR}/core_components_reflect.generated.hpp"
)
set(TENG_CORE_COMPONENT_REFLECT_GENERATED_CPP
    "${TENG_CORE_COMPONENT_REFLECT_OUT_DIR}/core_components_reflect.generated.cpp"
)
set(TENG_CORE_COMPONENT_REFLECT_MANIFEST
    "${TENG_CORE_COMPONENT_REFLECT_OUT_DIR}/core_components_reflect.generated.manifest.json"
)

add_custom_command(
    OUTPUT
        "${TENG_CORE_COMPONENT_REFLECT_GENERATED_HPP}"
        "${TENG_CORE_COMPONENT_REFLECT_GENERATED_CPP}"
        "${TENG_CORE_COMPONENT_REFLECT_MANIFEST}"
    COMMAND
        $<TARGET_FILE:teng-component-codegen>
        --out-dir "${TENG_CORE_COMPONENT_REFLECT_OUT_DIR}"
        --module-name "teng_core"
        --output-basename "core_components_reflect.generated"
        --namespace "teng::engine::core_component_generated"
        --function-prefix "core_components"
        --include "engine/scene/SceneComponents.hpp"
        --include "engine/Input.hpp"
        --header "${CMAKE_CURRENT_SOURCE_DIR}/engine/scene/SceneComponents.hpp"
        --header "${CMAKE_CURRENT_SOURCE_DIR}/engine/Input.hpp"
        --extra-arg=-std=c++23
        --extra-arg=-I${CMAKE_CURRENT_SOURCE_DIR}
        --extra-arg=-I${CMAKE_SOURCE_DIR}/third_party/flecs/include
        --extra-arg=-isystem${CMAKE_SOURCE_DIR}/third_party/glm
        --extra-arg=-I${CMAKE_SOURCE_DIR}/third_party/nlohmann_json/include
        ${TENG_COMPONENT_CODEGEN_SYSTEM_ARGS}
    DEPENDS
        teng-component-codegen
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/scene/SceneComponents.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/Input.hpp"
    COMMENT "Generating core component reflection code"
    VERBATIM
)

set(TENG_SCENE_SOURCES
    engine/scene/ComponentRegistry.cpp
    engine/scene/CoreComponentRegistrar.cpp
    engine/scene/ComponentSchema.cpp
    engine/scene/SceneComponentContext.cpp
    engine/scene/SceneSerializationContext.cpp
    engine/scene/Scene.cpp
    engine/scene/SceneComponents.cpp
    engine/scene/SceneIds.cpp
    engine/scene/SceneManager.cpp
    engine/scene/SceneCooked.cpp
    engine/scene/SceneSerialization.cpp
    "${TENG_CORE_COMPONENT_REFLECT_GENERATED_CPP}"
)

set(TENG_SCENE_AUTHORING_SOURCES
    engine/scene/authoring/SceneAuthoringCommand.cpp
    engine/scene/authoring/SceneAuthoringInspector.cpp
    engine/scene/authoring/SceneAuthoringJson.cpp
    engine/scene/authoring/SceneDocument.cpp
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

set(TENG_EDITOR_SOURCES
    editor/EditorLayer.cpp
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
    nlohmann_json::nlohmann_json
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
    target_include_directories(${target_name} SYSTEM PUBLIC ${CMAKE_SOURCE_DIR}/third_party/flecs/include)

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
add_teng_component(teng_content OFF ${TENG_CONTENT_SOURCES})
add_teng_component(teng_scene OFF ${TENG_SCENE_SOURCES})
target_include_directories(teng_scene PUBLIC "${TENG_CORE_COMPONENT_REFLECT_OUT_DIR}")
add_teng_component(teng_scene_authoring OFF ${TENG_SCENE_AUTHORING_SOURCES})
add_teng_component(teng_scene_tool_lib OFF engine/scene/SceneValidate.cpp)
add_teng_component(teng_render ON ${TENG_RENDER_SOURCES})
add_teng_component(teng_engine_runtime ON ${TENG_ENGINE_RUNTIME_SOURCES})
add_teng_component(lib_teng_editor OFF ${TENG_EDITOR_SOURCES})
add_library(teng_runtime INTERFACE)

target_link_libraries(teng_core PRIVATE fts_fuzzy_match)
target_link_libraries(teng_cvars PUBLIC teng_core PRIVATE implot Tracy::TracyClient)
target_link_libraries(teng_platform PUBLIC teng_core glfw PRIVATE imgui Tracy::TracyClient)
target_link_libraries(teng_gfx
    PUBLIC teng_core teng_cvars teng_platform imgui offsetAllocator
    PRIVATE teng_shader_compiler cgltf meshoptimizer ktx Tracy::TracyClient
)
target_link_libraries(teng_assets PUBLIC teng_core)
target_link_libraries(teng_content PUBLIC teng_core)
target_link_libraries(teng_scene PUBLIC teng_core teng_assets teng_content)
teng_link_flecs(teng_scene)
target_link_libraries(teng_scene_authoring PUBLIC teng_core teng_assets teng_scene)
target_link_libraries(teng_scene_tool_lib PUBLIC teng_core teng_assets teng_scene)
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
target_link_libraries(lib_teng_editor PUBLIC teng_engine_runtime teng_scene_authoring PRIVATE imgui)
target_link_libraries(teng_runtime INTERFACE teng_engine_runtime Tracy::TracyClient)
