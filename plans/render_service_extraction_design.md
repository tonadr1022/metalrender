# Render Service Extraction Design

Status: historical design note. Phase 3 render extraction, Phase 5 meshlet `IRenderer` extraction, and the Phase 6 asset/model residency bridge are implemented. `RenderService` now owns frame services, active renderer dispatch, and model residency; the normal `vktest` path no longer uses `gfx::TestRenderer`, `ITestScene`, `MeshletRendererScene`, or global `ResourceManager`. Use `plans/engine_runtime_migration_plan.md` and `plans/asset_registry_implementation_plan.md` for current sequencing.

Scope: track the renderer migration after the completed Phase 2 Flecs scene foundation. The first Phase 3 implementation introduced a renderer-neutral `RenderScene` snapshot, ECS extraction from `engine::Scene`, and an engine-owned `RenderService`/renderer boundary while preserving the current `vktest` meshlet demo.

Read with:

- `plans/engine_runtime_migration_plan.md`
- `plans/flecs_scene_foundation_design.md`

## Current Decisions

- `RenderScene` is a plain per-frame snapshot extracted from Flecs scene data. It is not a live ECS wrapper and not a meshlet GPU state container.
- Gameplay, editor, scene loaders, and ECS systems must not receive `gfx::RenderGraph`, RHI objects, `ModelGPUMgr`, bindless IDs, buffer handles, or texture handles.
- Render graph pass construction belongs to renderer code behind `RenderService`/`IRenderer`.
- The first implementation preserved `vktest`; those compatibility types have since been removed from the normal runtime path.
- Asset service and `ResourceManager` retirement details now live in `plans/asset_registry_implementation_plan.md`.
- Keep Vulkan and Metal viable by making new engine-facing renderer APIs backend-neutral and routing backend details through existing RHI/gfx abstractions.
- Put the engine-facing owner and frame boundary in `src/engine/render`; keep low-level, reusable renderer services in `src/gfx`; avoid putting `SceneManager` or Flecs knowledge into `gfx`.
- Move ImGui renderer ownership out of the compatibility meshlet final pass now, into a shared overlay/debug service path.
- Use a simple clear/debug renderer as the first diagnostic renderer for extracted data, with optional logging/inspection of `RenderScene`.
- Diff renderer instance allocation by stable `EntityGuid` from the start, rather than rebuilding instance buffers every frame when scene data has not changed.
- After path-derived scaffolding, durable `AssetId`s should come from a registry with generated stable IDs; importer metadata and source paths belong inside registry entries.

## Completed So Far

Implemented in the first Phase 3 slice:

- Added `src/engine/render/RenderScene.hpp` with renderer-neutral frame, camera, directional light, mesh, sprite, and aggregate `RenderScene` types.
- Added `src/engine/render/RenderSceneExtractor.*` to extract cameras, directional lights, meshes, and sprites from Flecs scene components.
- Extraction copies `LocalToWorld`, stores stable `EntityGuid`/`AssetId`, skips invalid entity GUIDs, skips invalid mesh/sprite asset IDs with counters, and sorts extracted vectors deterministically.
- Added `SpriteRenderable::sorting_order` so sprite extraction can sort by layer, order, then entity GUID.
- Added `src/engine/render/IRenderer.hpp` and `RenderFrameContext.hpp` as the renderer-facing boundary.
- Added `src/engine/render/RenderService.*` as an engine-owned shell that can extract the active scene, call an active `IRenderer`, bake/execute its `RenderGraph`, and present.
- Added `src/engine/render/DebugClearRenderer.*` as the first minimal renderer implementation for extracted-data diagnostics.
- `Engine` now owns `RenderService` and exposes it through `Engine::renderer()` and `EngineContext::renderer()`.
- Expanded `engine_scene_smoke` with device-free render extraction coverage for camera, light, mesh, sprite, invalid asset skipping, missing GUID skipping, transforms, IDs, tint/sort data, and deterministic ordering.
- Added data-first demo presets in `apps/common/ScenePresets.*`.
- Added `apps/vktest/DemoSceneEcsBridge.*` to author demo camera, directional light, mesh renderables, transforms, local-to-world matrices, names, and stable entity GUIDs into the active `engine::Scene`.
- Added a temporary demo `AssetId` to source-path registry for compatibility resource code.
- Moved preset world/model data ownership out of `MeshletRendererScene`; it now keeps meshlet controls, CSM/debug UI, and temporary camera/light tooling hooks.
- Historical: the first bridge added renderer/resource compatibility syncing in `TestRenderer`. Current code resolves registered `AssetId`s through `AssetService` and performs model residency in `RenderService`.
- Expanded `engine_scene_smoke` with procedural demo authoring coverage for valid assets, extraction, and stale entity cleanup.
- Validation passed with `./scripts/agent_verify.sh`; the current bounded runtime smoke is `./build/Debug/bin/metalrender --quit-after-frames 30`.

Implemented after this note:

- `TestRenderer` frame services moved into `RenderService`.
- `gfx::MeshletRenderer` became the default meshlet `IRenderer`.
- ImGui final composition moved under the `RenderService` overlay path.
- The normal meshlet demo dependency on `ITestScene`/`MeshletRendererScene` was removed.
- The temporary `AssetId -> source path -> ResourceManager` bridge was replaced by registered asset IDs, `AssetService`, and `RenderService` model residency.

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
- `SpriteRenderable { AssetId texture, tint, sorting_layer, sorting_order }`

Current Phase 3 engine/render code is present under `src/engine/render`:

- `RenderScene.hpp` defines the neutral snapshot schema.
- `RenderSceneExtractor.*` reads Flecs scene components into `RenderScene`.
- `RenderFrameContext.hpp` and `IRenderer.hpp` define the renderer-facing API.
- `RenderService.*` owns the first engine-facing render service shell.
- `DebugClearRenderer.*` provides a minimal clear renderer that consumes the new renderer boundary.
- `Engine` owns `RenderService`; `EngineContext` exposes it to layers.

Current `vktest` rendering no longer flows through the old compatibility renderer:

- `apps/vktest/TestApp.cpp` installs `CompatibilityVktestLayer` into `Engine`.
- `CompatibilityVktestLayer` handles preset hotkeys/UI and calls `ctx.renderer().enqueue_active_scene()` from `on_render()`.
- `RenderService` owns `ShaderManager`, `RenderGraph`, `ImGuiRenderer`, frame upload allocator, `BufferCopyMgr`, `ModelGPUMgr`, model residency, active `IRenderer`, and frame context.
- `gfx::MeshletRenderer` owns meshlet PSOs, CSM, depth pyramid, draw prep, readbacks, per-frame uniforms, renderer debug UI, and render graph pass construction.
- `apps/vktest/DemoSceneEcsBridge.*` converts demo preset data into ECS camera/light/mesh entities, resolves model asset IDs through `AssetDatabase`, and tracks authored demo entities for cleanup on preset reapply.

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

The first schema now lives in `src/engine/render/RenderScene.hpp`, under the engine-owned render boundary rather than under `apps/vktest`.

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

The first extraction implementation is directly testable without a device. `engine_scene_smoke` creates a scene with camera, light, mesh, and sprite components, ticks it, extracts a `RenderScene`, and verifies stable IDs, asset IDs, transforms, primary camera data, light data, sprite tint/sort data, invalid asset skipping, missing GUID skipping, and deterministic ordering.

## RenderService Fit

`RenderService` should be introduced as an engine-facing service under `src/engine/render`. It can be driven from the current layer model before `Engine::tick()` itself is reshaped, as long as layers above the render layer can access it through engine context/service access.

First integration shape:

- `Engine` owns `RenderService` now and exposes it through both `Engine::renderer()` and `EngineContext::renderer()`.
- `RenderService` gets references to `Window`, `rhi::Device`, `rhi::Swapchain`, resource paths, `SceneManager`, `EngineTime`, and ImGui enabled state.
- `CompatibilityVktestLayer` remains the bridge for current debug scenes while `RenderService` takes over frame services.
- New engine/data scene rendering uses `RenderScene` extraction; legacy `ITestScene` rendering remains explicitly compatibility-named.
- The first diagnostic renderer is `DebugClearRenderer`; it consumes `RenderFrameContext`/`RenderScene` and builds a simple clear pass.

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
- Temporary owner of FPS camera tooling, light/day-night controls, CSM, depth pyramid, draw prep, meshlet pass wiring, and meshlet debug UI.

Retire when:

- Meshlet pass construction lives in `MeshletRenderer`.
- Demo presets continue to create Flecs entities with stable IDs and asset references.
- Camera, light, and mesh data continue to flow through `RenderScene`.
- Renderer debug UI is separated from scene/preset/tooling UI.

### Global `ResourceManager`

Scaffolding role:

- Existing model cache and `ModelGPUMgr` bridge.

Retire or wrap when:

- Scene components store stable `AssetId`s only.
- An engine asset service resolves `AssetId` to CPU/source asset data.
- A renderer resource service owns GPU residency and model instance allocation.
- Runtime scene code no longer calls `ResourceManager::get()`.

## Asset Service Follow-Up

The asset service and renderer residency bridge have moved into Phase 6 and are tracked in `plans/asset_registry_implementation_plan.md`.

For Phase 3:

- `RenderScene` carries `AssetId` only.
- Path-derived `AssetId` remains acceptable only for legacy tests/tools; current demo model identity resolves through registered asset metadata.
- The long-term durable source is an asset registry with generated stable IDs. Source paths, importer type/version, import settings, and source content hashes are metadata on registry entries, not the identity itself.
- `ModelGPUMgr`, material allocation, texture upload queues, and instance allocation diffing remain behind renderer-owned code.
- ECS extraction must not call `ResourceManager`.
- Any temporary model-path compatibility bridge must be named as scaffolding and kept out of scene components.

Create a separate asset/resource service design note after one of these happens:

- The meshlet `IRenderer` owns the render path and still needs `AssetId` resolution beyond the temporary demo map.
- Multiple renderer/resource adapters need coordinated retirement criteria.

## First Implementation Slice Boundaries

Slice 1: `RenderScene` types and extraction.

Status: complete.

- Add renderer-neutral snapshot types.
- Add extraction from active Flecs scene components.
- Add extraction smoke tests.
- `vktest` behavior remains unchanged.

Slice 2: `RenderService` shell and compatibility renderer.

Status: partially complete.

Completed:

- Add engine-owned `RenderService` shell.
- Expose it through `Engine` and `EngineContext`.
- Add `IRenderer` and `RenderFrameContext`.
- Add basic render graph bake/execute/present orchestration for renderers owned by `RenderService`.
- Preserve scene cycling, preset loading, render graph debug dump, uploads, resize handling, and swapchain submission.
- Move or wrap `TestRenderer` frame services behind `RenderService` without changing legacy scene behavior.
- Keep `TestSceneContext` populated for existing `ITestScene` code, but name the bridge as compatibility.
- Add `IRenderer` with a compatibility implementation that delegates to the existing graph-pass path.
- Move ImGui renderer ownership into the shared overlay/debug service path while preserving current overlay behavior.

Slice 3: Render from extracted data in a minimal path.

Status: partially complete.

Completed:

- Let `RenderService` extract a `RenderScene` from the active `Scene`.
- Add the simple clear/debug renderer without moving meshlet pass ownership all at once.
- Not done but optional: Add `RenderScene` logging/inspection UI/path if useful.
- Keep meshlet demo rendering through the compatibility path until a migrated preset exists.

Slice 4: Migrate demo presets into ECS data.

Status: complete.

Completed:

- Converted the default meshlet demo presets into data-first `DemoScenePresetData`.
- Authored presets into Flecs entities with `Transform`, `LocalToWorld`, `Camera`, `DirectionalLight`, `MeshRenderable`, `Name`, and `EntityGuidComponent`.
- Used path-derived `AssetId` for model references and a temporary demo asset path registry for compatibility loading.
- Kept FPS camera/day-night behavior as compatibility tooling that mutates ECS camera/light components.
- Kept `ModelHandle`, GPU handles, bindless IDs, buffers, and meshlet resources out of ECS.
- Added smoke coverage for authoring, extraction, valid asset IDs, and stale entity cleanup.

Slice 5: Extract `MeshletRenderer`.

Status: next.

- Move meshlet pass construction, CSM, depth pyramid, draw prep, readbacks, and shade pass ownership into an `IRenderer` implementation.
- Consume `RenderScene` camera/light/mesh data.
- Resolve assets through renderer-owned compatibility/resource services.
- Diff renderer instance allocation by `EntityGuid` so unchanged model instances do not rewrite instance buffers every frame.
- Split renderer debug UI from scene/preset UI.
- Remove normal meshlet demo rendering from the `ITestScene`/`MeshletRendererScene` path once parity is preserved.

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

For documentation-only changes, no build is required.

For Phase 3 implementation slices:

```bash
./scripts/agent_verify.sh
```

Runtime smoke:

```bash
./build/Debug/bin/metalrender --quit-after-frames 30
```

Additional implementation checkpoints:

- Extraction smoke coverage for camera, directional light, mesh, sprite, invalid asset skipping, missing GUID skipping, and deterministic ordering is present in `engine_scene_smoke`.
- Confirm `engine_scene_smoke` remains in `agent_verify`.
- Exercise `vktest` scene cycling, preset loading, swapchain resize, render graph debug dump, ImGui overlay, culling toggles, and quit-after-frames after any compatibility bridge change.
- Run `teng-shaderc --all` through `agent_verify` after renderer/shader changes.
- Compare meshlet preset visual output before and after each bridge removal.
