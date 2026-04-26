# Meshlet IRenderer Extraction Plan

Status: planned. This is the implementation plan for moving normal meshlet rendering out of
`ITestScene` / `MeshletRendererScene` and into a real `IRenderer` implementation that consumes
`RenderScene`.

Read with:

- `plans/engine_runtime_migration_plan.md`
- `plans/render_service_extraction_design.md`
- `plans/flecs_scene_foundation_design.md`

## Goal

Create a production meshlet renderer boundary without changing the long-term scene model:

```text
Flecs scene components
  -> engine::RenderScene extraction
  -> engine::RenderService
  -> MeshletRenderer : engine::IRenderer
  -> renderer-owned resource residency and RenderGraph passes
```

The new renderer should preserve the current `vktest` meshlet demo behavior while removing the
normal render path's dependency on `ITestScene::add_render_graph_passes()` and
`MeshletRendererScene`.

## Non-Goals

- Do not replace `ResourceManager` with the final asset service in this slice.
- Do not add GPU/model handles, bindless indices, meshlet buffers, material allocators, or
  `RenderGraph` concepts to `RenderScene`.
- Do not turn `ITestScene` into the runtime scene abstraction.
- Do not redesign the RHI, `RenderGraph`, `ModelGPUMgr`, shader manager, or meshlet shaders.
- Do not implement a full editor viewport, multi-camera policy, or durable asset registry.
- Do not migrate the unrelated small debug scenes unless they block this extraction.

## Current State

Implemented foundations:

- `engine::RenderService` owns renderer-wide services: shader manager, render graph, frame staging,
  buffer copies, ImGui renderer, instance manager, static geometry batch, materials buffer,
  `ModelGPUMgr`, active `IRenderer`, and frame orchestration.
- `engine::RenderScene` carries renderer-neutral cameras, directional lights, mesh renderables,
  sprites, stable `EntityGuid`s, stable-ish `AssetId`s, and frame data.
- `RenderSceneExtractor` extracts ECS scene data without touching GPU resources.
- `gfx::TestRenderer` currently implements `engine::IRenderer`, but it is a compatibility wrapper.
- `TestRenderer` still owns an active `ITestScene` and forwards render pass construction to
  `scene_->add_render_graph_passes()`.
- `MeshletRendererScene` no longer owns demo model data, but it still owns the meshlet renderer
  runtime path: draw prep, depth pyramid, CSM, meshlet PSOs, meshlet visibility buffer, readbacks,
  per-frame uniform allocator, final shade pass, debug UI, FPS camera tooling, light/day-night
  tooling, and demo preset UI.
- `TestRenderer` contains a temporary renderer/resource bridge that diffs extracted
  `RenderScene::meshes` by `EntityGuid`, resolves `AssetId` through the demo asset map, and loads
  models through global `ResourceManager`.

## Target Ownership

### `engine::RenderService`

Owns shared renderer services and frame lifecycle:

- frame begin/end
- active `RenderScene` extraction
- active `IRenderer` dispatch
- render graph bake/execute/present
- upload/copy flushing
- ImGui overlay composition
- resize forwarding
- renderer debug UI dispatch

It should not permanently own renderer-specific GPU residency. The current
`static_instance_mgr_`, `static_draw_batch_`, `materials_buf_`, and `model_gpu_mgr_` members are
mesh/model-renderer-specific compatibility ownership. They may stay in `RenderService` only long
enough to preserve the existing bridge while `MeshletRenderer` is introduced. The target ownership
for these services is inside `MeshletRenderer` or a renderer-owned mesh resource/residency helper.

`RenderService` may continue to expose renderer-facing internals through `RenderFrameContext`.
That context is for `IRenderer` implementations only, not ECS systems, gameplay code, scene
loaders, or editor scene data.

### `MeshletRenderer`

New real renderer implementation. It owns:

- meshlet graphics PSOs
- meshlet visibility buffer
- meshlet draw prep
- depth pyramid
- CSM renderer
- static instance manager or renderer-owned access to instance allocation
- geometry batch storage for mesh/model data
- material GPU allocator/storage used by mesh rendering
- `ModelGPUMgr` or an equivalent renderer-owned model residency service
- frame uniform allocator
- meshlet readback buffers and stats
- meshlet final shade pass
- meshlet renderer debug UI
- temporary mesh resource residency bridge

It consumes:

- primary or fallback camera from `RenderScene`
- directional light from `RenderScene`
- mesh renderables from `RenderScene`
- output extent and frame index from `RenderFrameContext` / `RenderSceneFrame`

It does not consume:

- Flecs world or entities
- `ITestScene`
- `TestSceneContext`
- demo preset data directly
- scene/editor tooling input

### `MeshletRendererScene`

Migration scaffolding only. During the transition it may keep:

- FPS camera input and camera ECS sync
- day/night controls and light ECS sync
- demo preset list and preset application

It must stop owning:

- render graph pass construction
- meshlet GPU resources
- CSM/depth-pyramid/draw-prep renderer resources
- meshlet debug stats/readbacks
- final shade pass

Retirement criterion: normal meshlet demo rendering works when no `MeshletRendererScene` is
installed as the active rendering path. A small vktest controller may remain temporarily for
preset/camera tooling.

### `ResourceManager`

Temporary compatibility facade. It remains in this slice because replacing it before the real
meshlet renderer exists would mostly churn scaffolding.

Allowed in this slice:

- A compatibility resource helper inside renderer-owned code may call `ResourceManager::get()`.
- The helper must be clearly named as compatibility/scaffolding.
- Scene components and `RenderScene` must continue to store only `AssetId`, not `ModelHandle`.

Retirement criterion for later asset/resource work: `MeshletRenderer` owns the render path and the
remaining `AssetId -> path -> ResourceManager` bridge is isolated behind one or two named helpers.

## Proposed Files

Likely new files:

- `src/gfx/renderer/MeshletRenderer.hpp`
- `src/gfx/renderer/MeshletRenderer.cpp`
- `src/gfx/renderer/MeshletResourceCompatibility.hpp`
- `src/gfx/renderer/MeshletResourceCompatibility.cpp`
- `src/gfx/renderer/MeshletRenderData.hpp` if small shared request/data structs need a home

Likely moved or promoted files:

- `apps/vktest/scenes/MeshletDrawPrep.*` -> `src/gfx/renderer/MeshletDrawPrep.*`
- `apps/vktest/scenes/MeshletDepthPyramid.*` -> `src/gfx/renderer/MeshletDepthPyramid.*`
- `apps/vktest/scenes/MeshletCsmRenderer.*` -> `src/gfx/renderer/MeshletCsmRenderer.*`
- `apps/vktest/scenes/MeshletTestRenderUtil.*` -> `src/gfx/renderer/MeshletTestRenderUtil.*` or
  a less test-named meshlet utility file

Likely edited files:

- `src/CMakeLists.txt`
- `apps/vktest/CMakeLists.txt`
- `src/engine/render/RenderService.*`
- `src/engine/render/IRenderer.hpp` if debug UI dispatch needs a small API adjustment
- `src/engine/render/RenderFrameContext.hpp`
- `apps/vktest/TestApp.cpp`
- `apps/vktest/TestRenderer.*`
- `apps/vktest/TestDebugScenes.*`
- `apps/vktest/scenes/MeshletRendererTestScene.*`
- `apps/vktest/DemoSceneEcsBridge.*` only if preset/camera/light tooling needs a cleaner adapter

## Phase 1: Isolate Temporary Mesh Resource Residency

Move the model sync logic currently in `TestRenderer` into a renderer-owned compatibility helper.

Responsibilities:

- Track runtime model instances by `engine::EntityGuid`.
- For each `RenderMesh`, resolve `AssetId` through `demo_scene_compat::resolve_model_path`.
- Load new model instances through `ResourceManager::get().load_model(path, local_to_world)`.
- Update existing model transforms without reallocating when entity and asset are unchanged.
- Free removed model instances.
- Free all live instances on shutdown or renderer replacement.

Constraints:

- Keep this helper out of ECS extraction.
- Keep this helper out of `RenderScene`.
- Name it as compatibility scaffolding, for example `MeshletResourceCompatibility`.
- Keep one `EntityGuid` mapped to one model instance for this slice.

Exit criteria:

- `TestRenderer` no longer owns `runtime_models_`.
- Existing meshlet demo behavior is unchanged.
- Resource diffing can be reused by `MeshletRenderer`.

## Phase 1.5: Move Mesh Residency Services Out Of `RenderService`

Make explicit which `RenderService` members are truly global and which belong to the meshlet/model
renderer.

Keep in `RenderService`:

- `rhi::Device`
- `rhi::Swapchain`
- `Window`
- `SceneManager`
- resource directory
- `ShaderManager`, unless a later renderer selection design needs per-renderer shader managers
- `RenderGraph`
- frame upload allocator
- buffer copy manager
- ImGui renderer and overlay pass ownership
- active `IRenderer`
- frame index/output extent state

Move to `MeshletRenderer` or a renderer-owned mesh residency helper:

- `gfx::InstanceMgr`
- `gfx::GeometryBatch`
- `gfx::BackedGPUAllocator` used for mesh materials
- `gfx::ModelGPUMgr`
- mesh/model samplers that are not globally shared renderer defaults

Implementation shape:

- Construct these mesh-specific services from `MeshletRenderer` using `RenderFrameContext`'s
  global services: device, frame upload/copy services, shader manager, and resource paths.
- Remove `RenderService::model_gpu_mgr()` as a long-term public API. During migration, keep only a
  clearly named compatibility accessor if `ResourceManager::init()` still needs it before
  `MeshletRenderer` owns initialization.
- Keep pending buffer copy flushing in `RenderService`, because renderers can enqueue copies through
  the shared `BufferCopyMgr`.
- Keep pending texture upload flushing either in `RenderService` through a renderer callback or in
  `MeshletRenderer` through render graph/upload work. Do not leave `RenderService` hard-coded to
  `ModelGPUMgr` once `ModelGPUMgr` is renderer-owned.

Exit criteria:

- `RenderService` no longer directly owns `static_instance_mgr_`, `static_draw_batch_`,
  `materials_buf_`, or `model_gpu_mgr_` as permanent architecture.
- `RenderFrameContext` no longer exposes `model_gpu_mgr` as a generic global service, or the field
  is marked and used only as a temporary compatibility bridge.
- `MeshletRenderer` owns model GPU residency for the meshlet path.
- Other future renderers, such as a 2D renderer, are not forced to construct mesh/model residency
  services they do not use.

## Phase 2: Promote Meshlet Renderer Helpers Out Of `apps/vktest/scenes`

Move reusable meshlet renderer helpers into `src/gfx/renderer`.

Move first:

- `MeshletDrawPrep`
- `MeshletDepthPyramid`
- `MeshletCsmRenderer`
- meshlet draw/shade utility functions currently in `MeshletTestRenderUtil`

Rules:

- These files may depend on `gfx`, RHI, shader constants, and `ModelGPUMgr`.
- These files must not depend on `ITestScene`, `TestSceneContext`, `SceneManager`, or Flecs.
- Rename "test" labels only where the names are part of new renderer-owned code. Do not churn
  shader entry names unless needed.

Exit criteria:

- `MeshletRendererScene` includes renderer helpers from `src/gfx/renderer`.
- The helpers compile as part of the `teng` library instead of only the `vktest` app where
  practical.
- Existing `vktest` smoke run still works.

## Phase 3: Add `gfx::MeshletRenderer : engine::IRenderer`

Create the new renderer implementation.

Constructor/setup requirements:

- Receive or use only `RenderFrameContext` renderer services.
- Create meshlet PSOs using `ShaderManager`.
- Create readback buffers using `rhi::Device`.
- Initialize `MeshletDrawPrep`, `MeshletDepthPyramid`, `MeshletCsmRenderer`, and frame uniform
  allocator.
- Own meshlet culling flags and renderer debug config.

Frame render requirements:

- Sync resource compatibility models from `RenderScene::meshes`.
- Set `ModelGPUMgr` current frame index.
- Flush pending instance frees through a meshlet-renderer-owned transfer pass.
- Build meshlet draw passes from renderer-owned state.
- Shade into `frame.curr_swapchain_rg_id` so ImGui overlay composition can run after it.
- Handle empty or invalid scenes with a defined clear/no-renderable pass.

Resize requirements:

- Recreate or resize the depth pyramid from `RenderFrameContext::output_extent`.
- Avoid stale swapchain dimensions in persistent state.

Shutdown requirements:

- Release resource compatibility model instances.
- Shutdown CSM and depth pyramid resources.
- Release renderer-owned transient handles that are not automatically holder-managed.

Exit criteria:

- `MeshletRenderer` can render a `RenderScene` without `ITestScene`.
- `RenderGraph` pass construction for normal meshlet rendering is inside `MeshletRenderer`.
- `MeshletRendererScene::add_render_graph_passes()` is no longer needed for the normal meshlet
  demo path.

## Phase 4: Convert View/Light Inputs From Tooling State To `RenderScene`

Move camera/light reads out of `MeshletRendererScene` and into the renderer.

Camera policy for this slice:

- Prefer the first primary camera after deterministic extraction sorting.
- If no primary camera exists, use the first camera.
- If no camera exists, emit a clear/no-camera frame.
- Compute aspect from `RenderSceneFrame::output_extent`.
- Use the camera's `fov_y`, `z_near`, and `z_far` where available.
- Preserve reverse-Z/infinite projection behavior from the current meshlet path unless a shader
  assumption blocks it.

Light policy for this slice:

- Use the first directional light from `RenderScene`.
- Use `RenderDirectionalLight::direction` as the source of truth.
- Use the current default light if no directional light exists.
- Keep CSM defaults as renderer config for now, with demo preset tooling allowed to update them
  through a temporary vktest adapter if needed.

Required extraction from existing code:

- Move `prepare_view_data`, `prepare_cull_data`, and `prepare_cull_data_late` logic into
  `MeshletRenderer`, rewritten around `RenderCamera` and `RenderSceneFrame`.
- Keep meshlet-specific `ViewData` and `CullData` internal to the renderer.

Exit criteria:

- Moving the FPS camera changes ECS camera components, extraction sees the result, and
  `MeshletRenderer` renders from the extracted camera.
- Moving day/night lighting changes ECS directional light components, extraction sees the result,
  and `MeshletRenderer` renders from the extracted light.

## Phase 5: Split Renderer Debug UI From Scene/Preset Tooling

Move meshlet renderer diagnostics and renderer knobs to `MeshletRenderer::on_imgui()`.

Renderer UI:

- GPU object frustum cull toggle
- GPU object occlusion cull toggle
- CSM renderer UI
- visible task group readback
- visible object readback
- meshlet and triangle stats
- depth pyramid debug UI
- Resource compatibility counts if useful

Scene/tooling UI remains in vktest compatibility code:

- demo preset list
- load preset button
- FPS camera capture behavior
- day/night authoring controls if they are still treated as demo tooling

RenderService/UI dispatch:

- Ensure the active renderer's `on_imgui(RenderFrameContext&)` is called from the debug UI path.
- Keep final ImGui composition in `RenderService::enqueue_imgui_overlay_pass()`.
- Do not let scene code own the ImGui render pass.

Exit criteria:

- Meshlet renderer-specific debug UI is available without calling
  `MeshletRendererScene::on_imgui()`.
- Preset/camera tooling can still exist as compatibility UI without owning renderer internals.

## Phase 6: Install `MeshletRenderer` In `vktest`

Change the normal meshlet demo path to select the real renderer.

Recommended transition shape:

- `CompatibilityVktestLayer` creates `gfx::MeshletRenderer` and installs it through
  `ctx.renderer().set_renderer(...)`.
- A temporary vktest controller owns demo presets, FPS camera input, and light/day-night authoring.
- The controller writes to the active Flecs scene through `DemoSceneEcsBridge`.
- The render layer calls `ctx.renderer().enqueue_active_scene()` as it does today.

Handling the existing debug scene switcher:

- Keep small legacy `ITestScene` graphics experiments in `TestRenderer` if useful.
- Treat `TestRenderer` as a legacy/debug renderer, not the normal meshlet path.
- If tab-cycling is kept, switching to legacy scenes may install `TestRenderer`; switching back to
  meshlet should reinstall `MeshletRenderer`.
- Avoid making `MeshletRenderer` depend on `TestDebugScene`.

Exit criteria:

- Default `vktest` startup renders through `MeshletRenderer`.
- `vktest --quit-after-frames 30` does not need `MeshletRendererScene::add_render_graph_passes()`.
- Existing preset hotkeys still author ECS data and affect rendering.

## Phase 7: Reduce Or Retire `MeshletRendererScene`

After parity is reached, remove or sharply reduce `MeshletRendererScene`.

Acceptable end states:

- Preferred: delete `MeshletRendererScene` from the normal vktest render path and replace it with a
  small controller that has no renderer GPU state.
- Transitional: keep `MeshletRendererScene` only as `MeshletDemoController`-like scaffolding for
  camera/preset/light authoring, with no `add_render_graph_passes()` implementation used by normal
  rendering.

Must be removed from the normal path:

- meshlet pass construction
- meshlet PSOs
- depth pyramid resource ownership
- CSM resource ownership
- draw prep ownership
- meshlet visibility/readback buffers
- shade pass

Exit criteria:

- `ITestScene` is not required for normal meshlet rendering.
- `MeshletRendererScene` is no longer the owner of meshlet rendering.
- The remaining compatibility files are named and documented as scaffolding.

## Phase 8: Update Plans And Retirement Notes

Update the existing migration documents after implementation:

- `plans/engine_runtime_migration_plan.md`
- `plans/render_service_extraction_design.md`

Record:

- `MeshletRenderer` implementation status.
- Which compatibility helpers remain.
- Exact `ResourceManager` call sites that are still temporary.
- Which former `RenderService` mesh/model residency members moved into `MeshletRenderer`.
- Whether a separate asset/resource service plan should now be written.

## Validation

Run after each meaningful implementation slice:

```bash
./scripts/agent_verify.sh
```

Run the bounded app smoke test after renderer installation changes:

```bash
./build/Debug/bin/vktest --quit-after-frames 30
```

For shader or shared HLSL changes:

```bash
./build/Debug/bin/teng-shaderc --all
```

Behavioral checks:

- Default meshlet scene renders.
- Preset switching still changes visible world content.
- Camera movement still changes the rendered view.
- Day/night or manual light controls still affect lighting/shadows.
- CSM debug UI still works.
- Depth pyramid debug UI still works.
- Meshlet stats readbacks remain non-crashing and plausible after several frames.
- Empty/no-camera/no-mesh scenes present a defined clear frame instead of leaving swapchain state
  ambiguous.
- Resize recreates depth-pyramid-dependent resources correctly.

## Risks

- `MeshletRendererScene` mixes renderer state and demo tooling. Moving by file size instead of data
  flow could simply recreate the old coupling under a new class name.
- The temporary `ResourceManager` bridge can leak into scene data if `ModelHandle` or GPU handles
  are added to `RenderScene`. Do not do that.
- Leaving `ModelGPUMgr`, `GeometryBatch`, material allocation, or mesh instance allocation in
  `RenderService` would make future renderers inherit meshlet/model rendering assumptions. Move
  them behind `MeshletRenderer` as part of this migration, or clearly mark any temporary leftovers
  as compatibility debt with a removal point.
- `RenderFrameContext` exposes powerful renderer internals. Keep it behind `IRenderer`; do not pass
  it to ECS systems or scene loaders.
- The current meshlet path assumes one frame-global camera and light. Multi-camera/editor viewport
  support should wait until the single-view renderer boundary is stable.
- CSM defaults currently come from demo presets and renderer state. If preserving preset-specific
  CSM tuning becomes awkward, add a temporary vktest controller hook rather than extending
  `RenderScene` with CSM internals.
- `ResourceManager::get_model_from_cache()` can return null on failed loads, while callers assume
  valid results. The compatibility helper should handle failed asset resolution/load gracefully
  before passing data to meshlet draw code.
- Empty render scenes currently have weak behavior in the meshlet path. The new renderer should
  define this explicitly early.
- Moving helper files from `apps/vktest` into `src/gfx` can introduce reverse dependencies on app
  code. Keep app-only preset and camera tooling out of promoted files.
- Metal viability depends on the engine-facing API staying RHI-neutral. Keep backend-specific
  assumptions inside existing RHI/gfx code.

## Unresolved Questions

- Should `MeshletRenderer` live in namespace `teng::gfx` under `src/gfx/renderer`, or should there
  be an `engine::renderers` namespace for concrete engine renderers that sit above `gfx`? The
  current codebase suggests `teng::gfx` for this first renderer because the implementation is mostly
  graphics machinery.
- Should the temporary demo controller be a renamed/reduced `MeshletRendererScene`, or a new
  `MeshletDemoController` owned by `CompatibilityVktestLayer`? A new name is cleaner once render
  pass ownership is gone.
- Should legacy `TestRenderer` remain selectable in `vktest` after the default meshlet path moves
  to `MeshletRenderer`, or should it be split into a separate graphics experiment path later?
- Should CSM scene defaults become renderer config, scene metadata, or asset/preset metadata in the
  next slice? For this implementation, keep them as renderer/tooling config.
- Should the compatibility resource helper batch model loads with `load_instanced_models()` for
  initial preset loads? Start with the current one-entity/one-instance diff, then optimize only if
  profiling shows it matters.

## Completion Criteria

This migration is complete when:

- `MeshletRenderer` is a real `IRenderer` selected through `RenderService`.
- Normal meshlet rendering consumes `RenderScene` camera, light, and mesh data.
- Normal meshlet rendering does not call `ITestScene::add_render_graph_passes()`.
- Meshlet renderer GPU resources and render graph pass construction are owned by
  `MeshletRenderer`.
- Mesh/model residency services are no longer treated as global `RenderService` architecture.
- `MeshletRendererScene` is deleted or reduced to non-rendering compatibility tooling.
- Temporary `ResourceManager` usage is isolated behind named renderer/resource compatibility code.
- `./scripts/agent_verify.sh` passes.
- `./build/Debug/bin/vktest --quit-after-frames 30` passes.
