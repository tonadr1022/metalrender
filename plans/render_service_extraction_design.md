# Render Service Extraction Design

Status: design note for the renderer migration phase. This is planning-only and does not prescribe an implementation patch.

## Scope

This note narrows the renderer portion of `plans/engine_runtime_migration_plan.md` into a concrete extraction path from the current `apps/vktest` renderer harness toward:

- `engine::RenderService` as the engine-owned render frame service.
- A renderer-neutral `RenderScene` snapshot extracted from data-first Flecs scenes.
- `IRenderer` implementations that own render-graph pass construction.
- A first migrated implementation based on the current meshlet renderer behavior.

The current `TestRenderer`, `ITestScene`, `MeshletRendererScene`, and global `ResourceManager` usage remain compatibility scaffolding until the new runtime can preserve the existing meshlet demo behavior.

## Relevant Current Code

### `apps/vktest/TestRenderer.*`

`gfx::TestRenderer` currently combines several roles that should separate:

- Renderer service ownership: `ShaderManager`, `RenderGraph`, `ImGuiRenderer`, `GPUFrameAllocator3`, `BufferCopyMgr`, `ModelGPUMgr`, `InstanceMgr`, `GeometryBatch`, material storage, and default samplers.
- Frame orchestration: time delta calculation, dirty pipeline replacement, model texture upload flushing, render graph bake/execute, swapchain acquire/submit, and frame-in-flight advancement.
- Debug-scene orchestration: `ITestScene` lifetime, debug scene switching, cursor/key forwarding, scene presets, and scene ImGui.
- Temporary render pass wiring: `add_render_graph_passes()` forwards directly into `ITestScene::add_render_graph_passes()` after flushing pending instance frees.

The useful extraction source is the renderer service/frame orchestration code. The debug-scene host behavior should not become engine architecture.

### `apps/vktest/scenes/MeshletRendererTestScene.*`

`MeshletRendererScene` currently mixes:

- Scene data: model handles, camera, light direction, day/night settings, preset selection, CSM scene defaults.
- Tooling behavior: FPS camera input and debug ImGui.
- GPU residency assumptions: direct `ResourceManager::get().load_model/free_model()` calls and `ModelHandle` storage.
- Meshlet renderer state: pipelines, meshlet visibility buffer, draw stats readbacks, depth pyramid, CSM, draw prep, per-frame uniform allocator.
- RenderGraph pass construction: early and late meshlet passes, depth pyramid, CSM, shade pass, stats readbacks, and ImGui rendering into the final pass.

This class is the behavior reference for the first migrated renderer, but it should be split rather than promoted.

### `src/gfx/ModelGPUManager.*` And `src/ResourceManager.*`

`ModelGPUMgr` owns GPU model resources and instance allocations over `InstanceMgr`, `GeometryBatch`, `BufferCopyMgr`, and material storage. It loads CPU model data, uploads geometry/material/texture resources, creates instance GPU data, tracks pending texture uploads, and frees GPU allocations.

`ResourceManager` is a global singleton facade. It caches loaded models by path hash, creates per-instance `ModelInstance` copies, calls `ModelGPUMgr::add_model_instance()`, and returns runtime `ModelHandle`s.

Long term, scene data should not store `ModelHandle`s or depend on this singleton. Scene data should store stable asset IDs and transforms. `RenderService` and renderer-owned resource services should resolve those IDs into CPU assets and GPU residency.

### RenderGraph And Renderer Helpers

`src/gfx/RenderGraph.*` is already a renderer-side scheduling and resource lifetime primitive. Scene, gameplay, editor, and ECS extraction code should not receive it directly.

The newer `src/gfx/renderer` helpers already point toward a cleaner renderer internals layer:

- `GBufferRenderer` bakes render graph passes from `DrawPassSceneBindings` and view bindings.
- `DrawPassSceneBindings` separates draw batch/material/frame globals from pass-specific view state.
- `RenderView` packages view GPU resources such as cull data, depth pyramid, visibility buffers, and meshlet visibility.
- `ModelGPUUploader`, `InstanceMgr`, `BufferResize`, `CSM`, and renderer CVars are renderer-owned utilities.

The migration should reuse these helpers where practical, but they are still below `RenderScene`. They are not the scene schema.

## Target Architecture

Frame flow should become:

1. `Engine::tick()` advances time, input, layers, and the active Flecs scene.
2. A render extraction step reads ECS components and builds a renderer-neutral `RenderScene`.
3. `RenderService` prepares frame services and calls the active `IRenderer`.
4. The active renderer builds render graph passes, handles renderer-specific GPU data, and presents.

Ownership target:

- `engine::RenderService` owns frame-level renderer services: shader manager, render graph, upload allocators, copy manager, model GPU manager or its successor, default renderer resources, frame index, and active renderer.
- `IRenderer` owns renderer-specific persistent state and render graph pass construction.
- `RenderScene` is immutable for the render frame after extraction.
- Gameplay/editor/Flecs scene code never receives `RenderGraph`, `ModelGPUMgr`, `InstanceMgr`, or backend-specific RHI objects.
- ImGui rendering is a layer/service concern. Renderer debug panels may be exposed through a debug interface, but scene data UI should not live inside renderer pass wiring.

## What Moves Out Of `TestRenderer`

Move to `engine::RenderService` or a renderer service package:

- `ShaderManager` initialization, dirty pipeline replacement, and shutdown.
- `RenderGraph` initialization, debug dump request, bake, execute, and shutdown.
- Per-frame upload/copy services: `GPUFrameAllocator3`, `BufferCopyMgr`, pending buffer copies, pending texture upload flushing.
- `ModelGPUMgr`, `InstanceMgr`, `GeometryBatch`, material buffer, and default sampler creation while the current meshlet path still needs them.
- Swapchain-facing render frame orchestration: acquire image, bake graph for current window size, encode graph, submit frame, and advance frame-in-flight.
- Resize notification plumbing that tells renderers to recreate size-dependent resources.
- Renderer device info panel, if retained, as a generic debug panel.

Move to an ImGui/debug layer or debug service:

- ImGui context creation and font loading if the engine owns ImGui globally.
- `ImGuiRenderer` ownership if ImGui is treated as a shared engine overlay.
- Renderer debug panel dispatch.

Stay in `vktest` scaffolding:

- `TestDebugScene` selection and cycling.
- `ITestScene` lifetime and input forwarding.
- `apply_demo_scene_preset()` as a compatibility action.
- Direct scene overlay dispatch through `ITestScene::on_imgui()`.

Delete or reduce once migrated:

- `TestRenderer::scene_`, `active_scene_`, `set_scene()`, `cycle_debug_scene()`, and `create_test_scene()` usage in the runtime path.
- `TestSceneContext` as the way scenes access renderer internals.

## RenderScene Schema

`RenderScene` should be a plain frame snapshot, not a live ECS wrapper and not a meshlet-specific GPU state container.

Recommended first schema categories:

- Frame metadata: frame index, delta time if renderer effects need it, output extent, optional debug flags.
- Cameras: stable entity ID, local-to-world or world-to-view inputs, projection description, near/far, viewport, primary flag, render layer mask.
- Directional lights: stable entity ID, direction, color, intensity, optional shadow request/settings.
- Mesh renderables: stable entity ID, stable asset ID, local-to-world, visibility mask, material override handle or ID, renderer flags such as casts shadows.
- Sprite renderables: stable entity ID, texture asset ID, local-to-world or 2D transform, tint, sorting layer/order, material/blend mode.
- Extension blocks: typed optional payloads for renderer-specific data that is still sourced from scene components, not from renderer-owned GPU state.

Important boundaries:

- Store stable `AssetId` values, not `ModelHandle`, `ModelGPUHandle`, buffer handles, texture handles, or bindless indices.
- Store world-space or final extraction-space transforms so renderers do not need to query Flecs.
- Store semantic shadow settings, not CSM render resources.
- Keep meshlet-specific details out of the base mesh entry. A meshlet renderer can resolve a mesh asset to meshlet-capable GPU resources internally.
- Keep room for 2D from the start so the schema does not become "meshlet scene with extras."

Initial extraction can be simple and single-threaded. It should produce vectors with stable ordering, preferably by stable entity ID or explicit sort keys where deterministic rendering/debugging matters.

## RenderService API Shape

The design intent is a service boundary, not a new RHI abstraction.

Core responsibilities:

- Initialize and shut down renderer-global services from engine context.
- Own or reference `rhi::Device`, `rhi::Swapchain`, and platform window through engine context.
- Begin frame: update frame index, reset per-frame allocators, process pending renderer resource work.
- Extract or receive `RenderScene` from the active scene.
- Invoke `IRenderer::render(RenderFrameContext, const RenderScene&)`.
- Bake/execute `RenderGraph`, flush uploads, submit frame.
- Route swapchain resize events to the active renderer.
- Surface renderer debug UI without exposing `RenderGraph` to scenes.

`RenderFrameContext` should be renderer-facing and may include `rhi::Device`, `rhi::Swapchain`, `RenderGraph`, shader manager, upload allocators, copy manager, and resource residency services. It should not be passed to ECS gameplay systems.

## Path To MeshletRenderer

The first real renderer should be a `MeshletRenderer` that consumes `RenderScene` cameras, directional lights, and mesh renderables.

Migration target:

- Move `MeshletDrawPrep`, `MeshletDepthPyramid`, `MeshletCsmRenderer`, meshlet pipelines, meshlet visibility buffer, draw stats readbacks, and shade pass wiring behind `MeshletRenderer`.
- Reuse `GBufferRenderer`, `DrawPassSceneBindings`, and `RenderView` where they fit the migrated pass structure.
- Keep `ModelGPUMgr` as the temporary GPU residency backend, but hide it behind renderer/resource service calls.
- Convert mesh entries from `AssetId` plus transform into renderer-owned GPU instances before pass baking.
- Move camera view/projection creation from `FpsCameraController` into ECS camera extraction. The FPS controller can remain as tooling that updates a camera entity.
- Move directional light and day/night state into ECS components or tooling systems; CSM renderer receives extracted light data and renderer shadow settings.
- Split debug UI into renderer debug UI (culling, CSM, depth pyramid, draw stats) and scene/tooling UI (preset loading, camera controls, day/night authoring).

Compatibility bridge:

- A temporary meshlet adapter may still ask `MeshletRendererScene` to add render graph passes while `RenderService` is being introduced.
- That adapter must be named as compatibility scaffolding and should not receive new feature work beyond preserving behavior.
- The bridge should shrink in this order: frame orchestration first, scene data second, meshlet pass ownership last.

## Migration Scaffolding And Retirement Criteria

### `gfx::TestRenderer`

Scaffolding role:

- Existing renderer harness for `vktest`.
- Source for extracting `RenderService` responsibilities.
- Temporary host for legacy debug scenes.

Retire from runtime path when:

- `RenderService` owns frame renderer services and active renderer dispatch.
- `IRenderer` can render the meshlet demo path.
- Debug scene switching is test-only and no engine runtime code depends on `ITestScene`.

### `ITestScene`, `TestDebugScene`, And `TestSceneContext`

Scaffolding role:

- Legacy graphics test harness.
- Convenient way to keep existing small render experiments alive.

Retire from engine runtime when:

- Active runtime scenes are Flecs worlds.
- Render extraction produces `RenderScene`.
- Scenes no longer receive `RenderGraph`, RHI device, `ModelGPUMgr`, or `ShaderManager`.

They may remain in a separate graphics test executable if useful.

### `MeshletRendererScene`

Scaffolding role:

- Reference behavior for the meshlet renderer.
- Temporary owner of demo presets, camera/light data, meshlet pass wiring, and debug UI.

Retire when:

- Meshlet pass construction lives in `MeshletRenderer`.
- Demo presets create Flecs entities with stable IDs and asset references.
- Camera, light, and mesh data flow through `RenderScene`.
- Renderer debug UI is separated from scene/preset UI.

### Global `ResourceManager`

Scaffolding role:

- Existing model cache and `ModelGPUMgr` bridge.

Retire or wrap when:

- Scene components store stable `AssetId`s.
- Engine asset service resolves `AssetId` to CPU asset data.
- Renderer resource service owns GPU residency and model instance allocation.
- Runtime scene code no longer calls `ResourceManager::get()`.

## Phased Implementation Sequence

### Phase 0: Guardrail Plan

Deliverables:

- Land this note.
- Keep `./scripts/agent_verify.sh` and `vktest --quit-after-frames 30` as behavior guardrails for later code slices.

Exit criteria:

- Agreement that `RenderService`, `RenderScene`, and `IRenderer` are the migration target.

### Phase 1: Extract RenderService Without Changing Scene Model

Deliverables:

- Move frame service ownership from `TestRenderer` into a new render service while preserving the current `ITestScene` path.
- Keep `TestRenderer` as a thin compatibility shell or adapter.
- Keep `TestSceneContext` populated from the new service for legacy scenes.

Exit criteria:

- Existing debug scenes still run.
- Render graph debug dump, ImGui rendering, uploads, swapchain resize, and frame submission still work.
- `TestRenderer` no longer directly owns most renderer-global services.

### Phase 2: Introduce IRenderer And Compatibility Renderer

Deliverables:

- Add an `IRenderer` boundary below `RenderService`.
- Create a compatibility renderer that delegates to the existing scene pass path.
- Route renderer debug UI through the active renderer instead of direct scene calls where possible.

Exit criteria:

- `RenderService` can select an active renderer.
- Render graph pass construction is invoked through renderer code, even if the first renderer is still a bridge.
- Legacy scenes are clearly marked test-only.

### Phase 3: Define RenderScene And ECS Extraction

Deliverables:

- Add `RenderScene` snapshot types for cameras, directional lights, mesh renderables, and sprites.
- Add extraction from Flecs core components into `RenderScene`.
- Keep extraction independent from `RenderGraph`, RHI, and GPU model handles.

Exit criteria:

- A data scene can produce a `RenderScene` without invoking renderer internals.
- The schema can represent the current meshlet demo camera/light/mesh needs and a simple 2D scene.

### Phase 4: Move Demo Scene Data Out Of MeshletRendererScene

Deliverables:

- Convert at least one meshlet scene preset into Flecs entities.
- Store model references as stable asset IDs and transforms as ECS data.
- Move camera and directional light state to ECS components.
- Keep FPS camera and day/night as tooling/systems that update ECS data.

Exit criteria:

- The migrated preset renders from `RenderScene` data.
- `MeshletRendererScene::models_`, camera ownership, and light ownership are removed for the migrated path.

### Phase 5: Extract MeshletRenderer

Deliverables:

- Move meshlet pass construction and persistent renderer resources into `MeshletRenderer`.
- Consume `RenderScene` data and resolve assets through renderer resource services.
- Move CSM, depth pyramid, draw prep, stats readback, and shade pass ownership into the renderer.
- Split renderer debug UI from scene/preset UI.

Exit criteria:

- Meshlet rendering no longer depends on `ITestScene`.
- `MeshletRendererScene` is deleted or remains only as a test adapter outside the engine runtime.
- Current meshlet demo behavior is preserved through data scenes.

### Phase 6: Replace ResourceManager Singleton Boundary

Deliverables:

- Add or formalize an engine asset service and renderer GPU residency service.
- Replace direct `ResourceManager::get()` scene calls.
- Keep `ModelGPUMgr` as an internal renderer implementation detail until a fuller resource system replaces it.

Exit criteria:

- Scene data and extraction use stable asset IDs.
- Renderer code can load/resident models without a global singleton visible to scene code.

## Risks And Tradeoffs

- Extracting `RenderService` too aggressively could break existing demo behavior. Keep early slices behavior-preserving and compatibility-named.
- A meshlet-shaped `RenderScene` would block 2D and simple 3D goals. Keep the base schema renderer-neutral, with extension points for specialized renderers.
- `ModelGPUMgr` currently combines loading and GPU residency. Wrapping it too early may create churn, but exposing it to ECS would deepen the wrong dependency.
- RenderGraph lambdas capture pointers and references that must stay valid until execution. Moving pass construction into `IRenderer` should preserve clear ownership and frame lifetime rules.
- ImGui is currently intertwined with the renderer final pass. Moving ImGui to a layer/service requires care so overlays still render after scene color output.
- CSM defaults currently come from scene presets. The migration needs a data representation for shadow settings so renderer defaults remain predictable.
- Stable ordering of extracted renderables matters for reproducible debugging and future editor behavior. Avoid relying on incidental Flecs iteration order if that becomes unstable.

## Validation Strategy

For planning-only changes, no build is required.

For later implementation slices, run:

```bash
./scripts/agent_verify.sh
```

For runtime smoke testing, run:

```bash
./build/Debug/bin/vktest --quit-after-frames 30
```

Additional validation checkpoints for implementation phases:

- Compare the meshlet preset visual output before and after each bridge removal.
- Exercise scene preset loading, swapchain resize, render graph debug dump, ImGui overlay, and culling toggles.
- Verify shader compilation with `teng-shaderc --all` through `agent_verify.sh` after renderer/shader changes.
- Add small extraction tests once Flecs scene foundations exist: camera extraction, light extraction, mesh renderable extraction, sprite extraction, and deterministic ordering.

## Open Questions

- Should `RenderService` live under `src/engine` with engine context ownership, or under `src/gfx` with a narrow engine-facing wrapper?
- Is ImGui renderer ownership part of `RenderService`, an `ImGuiLayer`, or a shared debug overlay service?
- What is the first stable `AssetId` source: path-derived temporary IDs, a registry file, or an asset database stub?
- Should the first `IRenderer` interface own presentation, or should `RenderService` always own the final swapchain write and ImGui composition?
- How should renderer-specific extension data be represented in `RenderScene` without turning the base schema into a variant of every future renderer?
- Do meshlet CSM defaults belong in light components, renderer settings, or preset/tooling data?
- When `ModelGPUMgr` resolves multiple entities using the same mesh asset, should instance allocation be rebuilt every extraction or diffed incrementally from stable entity IDs?
