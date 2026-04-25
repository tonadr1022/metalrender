# Render Service Extraction Design

Status: decision-complete design note for Phase 3 implementation. This is planning-only; do not implement code from this note unless an implementation task explicitly asks for it.

Scope: prepare the renderer migration after the completed Phase 2 Flecs scene foundation. The next implementation phase should introduce a renderer-neutral `RenderScene` snapshot, ECS extraction from the active `engine::Scene`, and an engine-owned `RenderService`/renderer boundary while preserving the current `vktest` meshlet demo.

Read with:

- `plans/engine_runtime_migration_plan.md`
- `plans/flecs_scene_foundation_design.md`

## Current Decisions

- `RenderScene` is a plain per-frame snapshot extracted from Flecs scene data. It is not a live ECS wrapper and not a meshlet GPU state container.
- Gameplay, editor, scene loaders, and ECS systems must not receive `gfx::RenderGraph`, RHI objects, `ModelGPUMgr`, bindless IDs, buffer handles, or texture handles.
- Render graph pass construction belongs to renderer code behind `RenderService`/`IRenderer`.
- The first implementation must preserve `vktest` and may keep `TestRenderer`, `ITestScene`, `TestSceneContext`, `MeshletRendererScene`, and global `ResourceManager` as compatibility scaffolding.
- Do not create a separate asset service / `ResourceManager` retirement note yet. Keep a short deferral note here until the first `RenderScene` extraction creates real adapter names and call sites worth tracking separately.
- Keep Vulkan and Metal viable by making new engine-facing renderer APIs backend-neutral and routing backend details through existing RHI/gfx abstractions.
- Put the engine-facing owner and frame boundary in `src/engine/render`; keep low-level, reusable renderer services in `src/gfx`; avoid putting `SceneManager` or Flecs knowledge into `gfx`.
- Move ImGui renderer ownership out of the compatibility meshlet final pass now, into a shared overlay/debug service path.
- Use a simple clear/debug renderer as the first diagnostic renderer for extracted data, with optional logging/inspection of `RenderScene`.
- Diff renderer instance allocation by stable `EntityGuid` from the start, rather than rebuilding instance buffers every frame when scene data has not changed.
- After path-derived scaffolding, durable `AssetId`s should come from a registry with generated stable IDs; importer metadata and source paths belong inside registry entries.

## Relevant Current Code

Phase 2 scene foundation is present under `src/engine/scene`:

- `Scene` owns a `flecs::world`, registers core components, and ticks the world.
- `SceneManager` owns scenes, tracks the active scene, and is owned by `Engine`.
- `Engine::tick()` computes engine time, ticks the active scene, runs layer update/UI, and calls layer render.
- `engine_scene_smoke` validates scene creation, GUID lookup, `Transform` to `LocalToWorld`, entity destruction, and path-derived `AssetId`s.

Current core scene components are:

- `EntityGuidComponent { EntityGuid guid }`
- `Name { std::string value }`
- `Transform { translation, rotation, scale }`
- `LocalToWorld { glm::mat4 value }`
- `Camera { fov_y, z_near, z_far, primary }`
- `DirectionalLight { direction, color, intensity }`
- `MeshRenderable { AssetId model }`
- `SpriteRenderable { AssetId texture, tint, sorting_layer }`

Current `vktest` rendering still flows through compatibility scaffolding:

- `apps/vktest/TestApp.cpp` installs `CompatibilityVktestLayer` into `Engine`.
- `CompatibilityVktestLayer` creates `gfx::TestRenderer`, initializes global `ResourceManager` from `renderer_->get_model_gpu_mgr()`, forwards input/UI, and calls `TestRenderer::render()` from `on_render()`.
- `TestRenderer` owns `ShaderManager`, `RenderGraph`, `ImGuiRenderer`, frame upload allocator, `BufferCopyMgr`, `ModelGPUMgr`, `InstanceMgr`, `GeometryBatch`, material buffer, samplers, active `ITestScene`, and `TestSceneContext`.
- `TestSceneContext` exposes renderer internals directly to `ITestScene`.
- `MeshletRendererScene` owns demo model handles, FPS camera, directional light/day-night state, meshlet PSOs, CSM, depth pyramid, draw prep, readbacks, per-frame uniforms, debug UI, and render graph pass construction.
- `ResourceManager` is a global singleton facade that maps model paths to cached CPU-ish `ModelInstance` data and `ModelGPUMgr` GPU resources, then returns runtime `ModelHandle`s to scene code.

## Target Architecture

Target frame flow:

```text
Engine::tick()
  poll events and update EngineTime
  tick active SceneManager scene
  update layers and ImGui state
  RenderService extracts RenderScene from active Flecs scene
  RenderService prepares frame services
  active IRenderer builds renderer-owned RenderGraph work
  RenderService bakes/executes graph and presents
```

The implementation may reach this flow incrementally. The important Phase 3 boundary is that extracted data becomes renderer-neutral before render graph work starts.

Ownership target:

- `engine::RenderService` owns renderer frame orchestration and shared renderer services: shader manager, render graph, frame upload allocator, copy manager, model GPU manager or temporary adapter, default samplers/resources, frame index, active renderer, resize handling, and presentation.
- The engine-facing `RenderService`, `RenderScene`, extraction, `IRenderer`, and `RenderFrameContext` boundary lives under `src/engine/render`.
- Low-level reusable graphics machinery stays under `src/gfx`, including `RenderGraph`, shader management, model GPU residency, RHI-backed helpers, and renderer internals.
- `IRenderer` owns renderer-specific persistent resources and render graph pass construction.
- `RenderFrameContext` is renderer-facing and may include `rhi::Device`, `rhi::Swapchain`, `RenderGraph`, shader manager, upload/copy services, and temporary residency adapters.
- `RenderScene` is extracted before `IRenderer::render()` and treated as immutable for that frame.
- `Scene`, ECS systems, gameplay/editor code, and scene loaders only see scene components and stable IDs.

## RenderScene Schema

Create the first schema under engine-owned render/extraction code, not under `apps/vktest`. The exact file names are implementation choices, but the types should live with the engine/render service boundary rather than with meshlet test scenes.

Recommended shape:

```cpp
namespace teng::engine {

struct RenderSceneFrame {
  uint64_t frame_index{};
  float delta_seconds{};
  glm::uvec2 output_extent{};
};

struct RenderCamera {
  EntityGuid entity;
  glm::mat4 local_to_world{1.f};
  float fov_y{};
  float z_near{};
  float z_far{};
  bool primary{};
  uint32_t render_layer_mask{0xffffffffu};
};

struct RenderDirectionalLight {
  EntityGuid entity;
  glm::mat4 local_to_world{1.f};
  glm::vec3 direction{0.f, -1.f, 0.f};
  glm::vec3 color{1.f};
  float intensity{1.f};
  bool casts_shadows{true};
};

struct RenderMesh {
  EntityGuid entity;
  AssetId model;
  glm::mat4 local_to_world{1.f};
  uint32_t visibility_mask{0xffffffffu};
  bool casts_shadows{true};
};

struct RenderSprite {
  EntityGuid entity;
  AssetId texture;
  glm::mat4 local_to_world{1.f};
  glm::vec4 tint{1.f};
  int sorting_layer{};
  int sorting_order{};
};

struct RenderScene {
  RenderSceneFrame frame;
  std::vector<RenderCamera> cameras;
  std::vector<RenderDirectionalLight> directional_lights;
  std::vector<RenderMesh> meshes;
  std::vector<RenderSprite> sprites;
};

}  // namespace teng::engine
```

Schema decisions:

- Store stable `EntityGuid` and `AssetId`, not Flecs entity IDs or runtime GPU/model handles.
- Treat `EntityGuid` as the stable identity for one renderable model instance in the first mesh path. If a future entity expands into multiple renderer instances, that fan-out belongs in renderer/resource code keyed back to the source `EntityGuid`.
- Store final extraction-space transforms from `LocalToWorld`, so renderers do not query Flecs.
- Camera aspect and viewport come from `RenderSceneFrame::output_extent` for the first slice. Add explicit viewport fields later when multi-view/editor viewports require them.
- Directional light direction comes from `DirectionalLight::direction` for the first slice. `LocalToWorld` is included so a later transform-derived rule can be added without changing renderer ownership.
- Shadow data starts as semantic booleans/defaults, not CSM resources. Meshlet CSM defaults are scene-specific, so scene metadata should grow renderer-feature settings for them before migrated presets rely on CSM tuning.
- Sprite data is included now even if the first renderer ignores it, so the base schema does not become meshlet-shaped.
- Keep meshlet-specific data out of base entries. Meshlet renderers resolve `AssetId` to meshlet-capable GPU resources internally.
- Add renderer extension blocks only after a concrete renderer needs them; do not begin Phase 3 with an open-ended variant container.

## ECS Extraction Flow

Extraction reads the active `Scene` after `SceneManager::tick_active_scene()` has run, so `LocalToWorld` is current.

Initial queries:

- Cameras: `EntityGuidComponent`, `LocalToWorld`, `Camera`
- Directional lights: `EntityGuidComponent`, `LocalToWorld`, `DirectionalLight`
- Meshes: `EntityGuidComponent`, `LocalToWorld`, `MeshRenderable`
- Sprites: `EntityGuidComponent`, `LocalToWorld`, `SpriteRenderable`

Extraction rules:

- Prefer `EntityGuidComponent` from the entity over any side lookup. Future loaded scenes may not have been created through `Scene::create_entity()`.
- Skip entities missing a valid `EntityGuidComponent`.
- Skip mesh/sprite entries with invalid `AssetId`, but keep a debug counter/log path so bad data is visible.
- Copy `LocalToWorld::value` directly. Transform hierarchy remains deferred.
- If multiple cameras are marked primary, choose deterministic ordering and let the renderer pick the first primary. If no primary exists, renderers may choose the first camera or produce a clear/no-camera frame.
- Sort each extracted vector by `EntityGuid::value` for deterministic debugging and future editor behavior. Sprite render order should sort by `sorting_layer`, then `sorting_order`, then `EntityGuid`.
- Extraction must not allocate GPU resources, load models, mutate `ResourceManager`, build render graph passes, or call RHI.
- Renderer/resource residency should diff extracted renderables by `EntityGuid` so unchanged model instances do not rewrite instance buffers every frame. This diff belongs after extraction, inside renderer-owned resource code, not in ECS extraction.

The first extraction implementation should be directly testable without a device. Add smoke coverage that creates a scene with camera, light, mesh, and sprite components, ticks it, extracts a `RenderScene`, and verifies stable IDs, asset IDs, transforms, primary camera data, light data, sprite tint/sort data, and deterministic ordering.

## RenderService Fit

`RenderService` should be introduced as an engine-facing service under `src/engine/render`. It can be driven from the current layer model before `Engine::tick()` itself is reshaped, as long as layers above the render layer can access it through engine context/service access.

First integration shape:

- `Engine` should own `RenderService` once custom renderer selection needs to be engine-wide, such as voxel/custom render implementations. Before that, the first slice may host it from a render layer to reduce `vktest` churn if the service is still exposed to layers above it.
- `RenderService` gets `EngineContext` references to `Window`, `rhi::Device`, `rhi::Swapchain`, resource paths, `SceneManager`, `EngineTime`, and ImGui enabled state.
- `CompatibilityVktestLayer` remains the bridge for current debug scenes while `RenderService` takes over frame services.
- New engine/data scene rendering uses `RenderScene` extraction; legacy `ITestScene` rendering remains explicitly compatibility-named.
- The first diagnostic renderer should be a simple clear/debug renderer that can optionally log or inspect the extracted `RenderScene`.

Renderer-facing API intent:

```cpp
class IRenderer {
 public:
  virtual ~IRenderer() = default;
  virtual void on_resize(RenderFrameContext& frame) {}
  virtual void render(RenderFrameContext& frame, const RenderScene& scene) = 0;
  virtual void on_imgui(RenderFrameContext& frame) {}
};
```

`RenderFrameContext` may expose renderer internals. It must not be passed to ECS systems, gameplay/editor scene code, or scene loaders.

`RenderService` responsibilities:

- Initialize and shut down renderer-global services.
- Update frame index and per-frame allocators.
- Replace dirty pipelines.
- Extract or receive `RenderScene` for the active scene.
- Let the active renderer build graph passes.
- Bake the render graph for the current output extent.
- Acquire swapchain image, flush pending buffer copies and texture uploads, execute graph, submit frame, and advance frame-in-flight.
- Route resize events to active renderer and size-dependent shared resources.
- Provide renderer debug UI without exposing `RenderGraph` outside renderer code.

ImGui rule:

- Move ImGui renderer ownership now from the compatibility meshlet final pass into a shared overlay/debug service path.
- Preserve the existing `Engine` ImGui frame lifecycle while moving the renderer object and final overlay composition out of `MeshletRendererScene`.
- Scene code must not own the ImGui render pass. Renderer debug panels may contribute UI, but final composition belongs to the overlay/debug service path coordinated by `RenderService`.

## Compatibility Scaffolding

### `gfx::TestRenderer`

Scaffolding role:

- Existing renderer harness for `vktest`.
- Source for extracting `RenderService` services and frame orchestration.
- Temporary host for legacy `ITestScene` debug scenes.

Retire from runtime path when:

- `RenderService` owns shared frame services and active renderer dispatch.
- Meshlet rendering can run through `IRenderer`.
- Debug scene switching is test-only and no engine runtime code depends on `ITestScene`.

### `ITestScene`, `TestDebugScene`, And `TestSceneContext`

Scaffolding role:

- Legacy graphics test harness.
- Temporary compatibility bridge that can still receive `RenderGraph` while existing scenes are being migrated.

Retire from engine runtime when:

- Active runtime scenes are Flecs worlds.
- New render paths consume `RenderScene`.
- Scenes no longer receive `RenderGraph`, RHI device, `ModelGPUMgr`, `ShaderManager`, upload allocators, or ImGui renderer.

They may remain in a separate graphics test executable if useful.

### `MeshletRendererScene`

Scaffolding role:

- Reference behavior for the meshlet renderer.
- Temporary owner of demo presets, FPS camera, light/day-night controls, model handles, CSM, depth pyramid, draw prep, meshlet pass wiring, and meshlet debug UI.

Retire when:

- Meshlet pass construction lives in `MeshletRenderer`.
- Demo presets create Flecs entities with stable IDs and asset references.
- Camera, light, and mesh data flow through `RenderScene`.
- Renderer debug UI is separated from scene/preset/tooling UI.

### Global `ResourceManager`

Scaffolding role:

- Existing model cache and `ModelGPUMgr` bridge.

Retire or wrap when:

- Scene components store stable `AssetId`s only.
- An engine asset service resolves `AssetId` to CPU/source asset data.
- A renderer resource service owns GPU residency and model instance allocation.
- Runtime scene code no longer calls `ResourceManager::get()`.

## Asset Service Deferral

Do not create a separate asset service / `ResourceManager` retirement plan before the first `RenderScene` extraction. The current design already has enough retirement criteria, and a separate note would mostly duplicate this file until implementation creates concrete compatibility adapters.

For Phase 3:

- `RenderScene` carries `AssetId` only.
- Path-derived `AssetId` remains acceptable scaffolding for demos and tests.
- The long-term durable source is an asset registry with generated stable IDs. Source paths, importer type/version, import settings, and source content hashes are metadata on registry entries, not the identity itself.
- `ResourceManager`, `ModelGPUMgr`, material allocation, texture upload queues, and instance allocation diffing remain behind compatibility or renderer-owned code.
- ECS extraction must not call `ResourceManager`.
- Any temporary `AssetId` to model-path or model-handle bridge must be named as compatibility scaffolding and kept out of scene components.

Create a separate asset/resource service design note after one of these happens:

- A `RenderService` compatibility adapter owns real `AssetId` resolution call sites.
- A migrated Flecs demo preset needs an `AssetId` to source path registry beyond path-derived IDs.
- Multiple renderer/resource adapters need coordinated retirement criteria.

## First Implementation Slice Boundaries

Slice 1: `RenderScene` types and extraction.

- Add renderer-neutral snapshot types.
- Add extraction from active Flecs scene components.
- Add extraction smoke tests.
- No `RenderService` ownership changes required in this slice.
- No `vktest` behavior changes.

Slice 2: `RenderService` shell and compatibility renderer.

- Move or wrap `TestRenderer` frame services behind `RenderService` without changing legacy scene behavior.
- Keep `TestSceneContext` populated for existing `ITestScene` code, but name the bridge as compatibility.
- Add `IRenderer` with a compatibility implementation that delegates to the existing graph-pass path.
- Move ImGui renderer ownership into the shared overlay/debug service path while preserving current overlay behavior.
- Preserve scene cycling, preset loading, render graph debug dump, uploads, resize handling, and swapchain submission.

Slice 3: Render from extracted data in a minimal path.

- Let `RenderService` extract a `RenderScene` from the active `Scene`.
- Add the simple clear/debug renderer and optional `RenderScene` logging/inspection path without moving meshlet pass ownership all at once.
- Keep meshlet demo rendering through the compatibility path until a migrated preset exists.

Slice 4: Migrate one demo preset into ECS data.

- Convert one meshlet preset into Flecs entities with `Transform`, `LocalToWorld`, `Camera`, `DirectionalLight`, and `MeshRenderable`.
- Use stable `AssetId` for model references.
- Keep FPS camera/day-night behavior as tooling systems or compatibility layer behavior that mutates ECS components.
- Do not store `ModelHandle` or GPU handles in ECS.

Slice 5: Extract `MeshletRenderer`.

- Move meshlet pass construction, CSM, depth pyramid, draw prep, readbacks, and shade pass ownership into an `IRenderer` implementation.
- Consume `RenderScene` camera/light/mesh data.
- Resolve assets through renderer-owned compatibility/resource services.
- Diff renderer instance allocation by `EntityGuid` so unchanged model instances do not rewrite instance buffers every frame.
- Split renderer debug UI from scene/preset UI.

## Risks And Tradeoffs

- `RenderGraph` is deeply exposed to `ITestScene`; early slices can accidentally rename the old architecture. Keep graph access inside renderer or explicitly compatibility-named bridge code.
- `MeshletRendererScene` mixes scene data, tooling, resource lifetime, GPU resources, pass wiring, and debug UI. Split it by data flow, not by file size.
- `RenderScene` could accidentally mirror `ModelGPUMgr` or meshlet draw buffers. Keep GPU residency, draw batches, material buffers, bindless IDs, and meshlet visibility out of the schema.
- ImGui composition is currently coupled to the final meshlet shade pass. Move ownership to the shared overlay/debug service path now while preserving the current overlay behavior.
- Camera data currently lacks viewport/aspect policy. Use output extent first; add explicit viewport fields when editor/multi-view work needs them.
- Directional light has both a direction component and a transform. Use component direction first and keep transform available for future policy changes.
- `ResourceManager` uses path hashing and runtime handles that are not durable scene data. Treat it as scaffolding until an asset registry exists.
- Instance lifetime is nontrivial. Diff by stable `EntityGuid` from the start, and keep that state inside renderer-owned resource code so ECS extraction remains a snapshot operation.
- Empty scenes currently may produce no render graph passes in the meshlet path. `RenderService` should eventually define a clear/no-camera/no-renderables presentation behavior.
- Metal viability depends on keeping engine-level APIs RHI-neutral and avoiding Vulkan-specific assumptions in `RenderService`.

## Validation Strategy

For this planning-only change, no build is required.

For Phase 3 implementation slices:

```bash
./scripts/agent_verify.sh
```

Runtime smoke:

```bash
./build/Debug/bin/vktest --quit-after-frames 30
```

Additional implementation checkpoints:

- Add extraction tests for camera, directional light, mesh, sprite, invalid asset skipping, missing GUID skipping, and deterministic ordering.
- Confirm `engine_scene_smoke` remains in `agent_verify`.
- Exercise `vktest` scene cycling, preset loading, swapchain resize, render graph debug dump, ImGui overlay, culling toggles, and quit-after-frames after any compatibility bridge change.
- Run `teng-shaderc --all` through `agent_verify` after renderer/shader changes.
- Compare meshlet preset visual output before and after each bridge removal.
