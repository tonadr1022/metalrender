# Engine Runtime Migration Plan

Status: Phase 5 (meshlet `IRenderer` extraction) is implemented. `gfx::MeshletRenderer` is the default active renderer via `engine::RenderService`; meshlet passes, PSOs, CSM, depth pyramid, and draw prep live under `src/gfx/renderer/`. Demo preset and `ResourceManager` wiring for `vktest` remains in compatibility code (`CompatibilityVktestLayer`, `DemoSceneEcsBridge`, `demo_scene_compat`). The next major steps are the asset/resource service boundary (Phase 6), optional tightening of `ModelGPUMgr` ownership (still constructed by `RenderService` today), then 2D and editor foundations.

This plan treats the engine as a long-term runtime/editor foundation. `vktest` is a thin `Engine` host with a compatibility layer, not the destination architecture. The destination is a tick-driven, layer-composed engine with data-first Flecs scenes, stable IDs, renderer services, and an editor layer on top of the same runtime. Legacy `TestRenderer` / `ITestScene` / `MeshletRendererTestScene` types from earlier drafts are no longer in the tree; do not reintroduce them as patterns.

## Decisions From Draft Review

- The engine should expose `tick()` as the first-class execution primitive. A standalone `run()` helper is fine, but editor/play-mode control needs manual ticking.
- Scenes should be data-first. The long-term scene model is not C++ scene subclasses; it is serialized scene data loaded into a Flecs world.
- Game behavior should come from systems and scripts. Lua is out of scope for now, but the architecture should not require writing game scenes in C++.
- Flecs should be a git submodule under `third_party`, added to `.gitmodules`.
- Metal support should be preserved if the existing platform/window/device abstractions make that practical. New engine code should avoid Vulkan-only assumptions.
- The first real use cases are 2D games and simple 3D platformers. Meshlet rendering remains important, but the shared scene/render abstraction cannot be meshlet-shaped only.
- The editor should be a layer on top of the runtime, enabled by build configuration, not a separate architecture.
- Stable scene/entity IDs and asset handles should be introduced early if delaying them would cause broad future churn. The plan assumes they are phase-one foundations.

## Goals

- Introduce an engine runtime with explicit ownership of platform services, timing, input, scene state, resources, renderer services, and layer orchestration.
- Use Flecs as the scene ECS from the beginning of the runtime scene model.
- Keep the existing meshlet renderer path working during migration.
- Preserve Metal and Vulkan as supported backend goals through RHI/platform factories.
- Support data-first 2D and simple 3D scenes before optimizing for editor richness.
- Make the renderer boundary broad enough for mesh renderables, sprites/2D renderables, cameras, lights, and later custom renderer hooks.
- Preserve `vktest --quit-after-frames 30` and `./scripts/agent_verify.sh` as migration guardrails.
- Mark temporary compatibility code clearly so it can be deleted deliberately.

## Non-Goals For Phase One

- Lua scripting implementation.
- Full editor UI.
- Complete scene serialization format.
- Full custom shader/material graph system.
- Full custom renderer plugin system.
- Rewriting the RHI, render graph, shader manager, or meshlet renderer internals.
- Replacing every debug scene immediately.

## Current State

### `TestApp`

`apps/vktest/TestApp.cpp` is a thin host over `engine::Engine`: it builds `EngineConfig`, pushes `CompatibilityVktestLayer` and `ImGuiOverlayLayer`, and calls `Engine::run()`. Global `ResourceManager` init/shutdown and demo preset hotkeys live in the compatibility layer. Long term this executable can stay as a smoke-test entry or shrink further once bootstrap is fully generic.

### `CompatibilityVktestLayer` (in `TestApp.cpp`)

Temporary `engine::Layer` for `vktest`: barrier self-test, renderer CVars, `ResourceManager::init` with `ctx.renderer().model_gpu_mgr()`, demo RNG seed, default scene preset load, Ctrl+0–9 preset hotkeys, `on_render` → `ctx.renderer().enqueue_active_scene()`, per-frame `demo_scene_compat::sync_loaded_model_transforms`, and ImGui for app chrome plus preset list / meshlet panels (`ctx.renderer().on_imgui()`). This is demo/tooling, not the engine runtime model.

### `engine::RenderService` and `gfx::MeshletRenderer`

`RenderService` (`src/engine/render/`) owns shared infrastructure: `ShaderManager`, `RenderGraph`, `GPUFrameAllocator3`, `BufferCopyMgr`, `ImGuiRenderer`, `ModelGPUMgr` (shared upload path; still engine-owned for now), default-constructed `gfx::MeshletRenderer` as the active `IRenderer`, frame context, and extract-and-render in `enqueue_active_scene()`. `MeshletRenderer` implements meshlet passes and reads cameras, lights, and extent from `RenderScene` (see `src/gfx/renderer/MeshletRenderer.*`). There is no separate vktest `TestRenderer` class anymore.

### Historical note

Earlier migrations referred to `gfx::TestRenderer`, `ITestScene`, and `MeshletRendererTestScene` / `MeshletRendererScene`. Those types have been removed; meshlet rendering and the old “debug scene” harness were folded into `RenderService` + `MeshletRenderer` + `CompatibilityVktestLayer` / `DemoSceneEcsBridge`.

## Destination Architecture

```text
Executable
  parses command line
  builds EngineConfig
  installs layers
  calls Engine::run() or drives Engine::tick()

Engine
  owns platform/window/device/swapchain
  owns time, input, resources, scenes, renderer service
  owns ordered layer stack
  exposes tick() as the primary API

LayerStack
  Core runtime layer
  Render layer
  Optional ImGui/debug layer
  Optional editor layer
  Future scripting layer

SceneManager
  loads/unloads data scenes
  owns active/edit/play worlds
  manages scene reload and play-mode transitions

Scene
  data-first Flecs world
  stable entity IDs
  core components
  runtime component registration path

RenderService
  owns renderer backend services
  extracts RenderScene from ECS
  invokes active renderer implementation
  presents frames

IRenderer implementations
  Meshlet renderer first through compatibility extraction
  2D renderer later
  voxel/custom render hooks later
```

The important shift is that `Scene` is not a polymorphic gameplay object. It is loaded data plus registered ECS systems. Temporary C++ adapters are allowed only to bridge existing demos.

## Layer Model

The engine should use layers because runtime, debug UI, editor, and future scripting need different build/run compositions without forking the architecture.

For Phase 1 tick/layer ownership boundaries, layer ordering, and ImGui lifecycle constraints (captured during the runtime shell work), see `plans/engine_tick_layer_design.md`.

Initial API sketch:

```cpp
namespace teng::engine {

class Layer {
 public:
  virtual ~Layer() = default;
  virtual void on_attach(EngineContext& ctx) {}
  virtual void on_detach(EngineContext& ctx) {}
  virtual void on_update(EngineContext& ctx, const EngineTime& time) {}
  virtual void on_imgui(EngineContext& ctx) {}
  virtual void on_event(EngineContext& ctx, const InputEvent& event) {}
};

class LayerStack {
 public:
  void push_layer(std::unique_ptr<Layer> layer);
  void push_overlay(std::unique_ptr<Layer> overlay);
};

}  // namespace teng::engine
```

Expected early layers:

- `RuntimeLayer`: advances simulation for the active scene when simulation is running.
- `RenderLayer`: extracts render data and presents.
- `ImGuiDebugLayer`: owns generic debug panels and developer toggles.
- `EditorLayer`: build-flagged layer that inspects and edits scene data.
- `CompatibilityVktestLayer`: temporary `vktest` layer for demo presets, `ResourceManager` bootstrap, and UI hooks; rendering goes through `RenderService` (`enqueue_active_scene`).

The layer stack should support editor play/stop by allowing the editor layer to pause simulation updates while still ticking input, UI, resource processing, and rendering.

## Engine Runtime

`Engine::tick()` should be the primitive. `Engine::run()` should be a small helper for standalone executables.

Initial API sketch:

```cpp
namespace teng::engine {

struct EngineConfig {
  std::filesystem::path resource_dir;
  gfx::rhi::GfxAPI preferred_gfx_api;
  std::string app_name;
  bool enable_imgui = true;
  bool enable_editor = false;
  bool vsync = true;
  int initial_window_width = -1;
  int initial_window_height = -1;
};

class Engine {
 public:
  explicit Engine(EngineConfig config);
  ~Engine();

  void init();
  void shutdown();

  // Returns false when the platform window requests close.
  bool tick();

  // Convenience wrapper for app targets and smoke tests.
  void run(std::optional<uint32_t> quit_after_frames = std::nullopt);

  EngineContext& context();
  LayerStack& layers();
  SceneManager& scenes();
  RenderService& renderer();
};

}  // namespace teng::engine
```

Engine-owned responsibilities:

- Resource directory and local resource directory setup.
- CVar load/save.
- Window creation through platform abstraction.
- Device creation through `gfx::rhi::create_device`.
- Swapchain creation.
- Main frame timing and frame index.
- Input event collection/dispatch.
- ImGui context/frame lifecycle if an ImGui layer is installed.
- Layer attach/detach/update order.
- Coordinated shutdown.

Backend support rule: `Engine` may choose Vulkan or Metal through config/factory selection, but engine-level code should not include backend-specific renderer logic unless isolated behind platform/backend implementations.

## Data-First Scenes

Long-term `Scene` should be a data container around a Flecs world, not a game-specific subclass.

Initial shape:

```cpp
namespace teng::engine {

struct SceneId {
  uint64_t value;
};

struct EntityGuid {
  uint64_t value;
};

class Scene {
 public:
  explicit Scene(SceneId id);

  SceneId id() const;
  flecs::world& world();
  const flecs::world& world() const;

  flecs::entity create_entity(EntityGuid guid, std::string_view name);
  void destroy_entity(EntityGuid guid);
  flecs::entity find_entity(EntityGuid guid) const;
};

}  // namespace teng::engine
```

Scene behavior should come from:

- Core engine ECS systems.
- Renderer extraction systems.
- Future script systems.
- Optional C++ native systems registered by a game module.

Temporary migration adapters may create data procedurally, but those adapters should be named as compatibility/demo loaders, not as the final scene abstraction.

## Stable IDs And Handles

Stable IDs should start early.

Phase-one minimum:

- `SceneId`: stable identity for a scene asset or generated demo scene.
- `EntityGuid`: stable entity identity stored as a Flecs component.
- `AssetId`: stable identity for source assets.
- Runtime handles may remain separate from stable IDs.

Important distinction:

- Stable IDs are for saved/referenceable data.
- Flecs entity IDs are runtime identities.
- GPU/model handles are runtime resource identities.

The first implementation can generate GUID-like 64-bit IDs locally. It does not need a final UUID library unless the asset pipeline requires one.

## Flecs Integration

Flecs should be added as a git submodule under `third_party/flecs` and wired through CMake.

Integration principles:

- Core scene data lives in `flecs::world`.
- Core components are registered by the engine.
- Runtime component registration must stay possible for editor-authored and future script-authored data.
- Avoid wrapping Flecs so heavily that editor metadata and dynamic components become inaccessible.
- Avoid exposing Flecs directly from renderer internals where a render snapshot is cleaner.

Initial components:

```cpp
struct EntityGuidComponent {
  EntityGuid guid;
};

struct Name {
  std::string value;
};

struct Transform {
  glm::vec3 position{0.f};
  glm::quat rotation{1.f, 0.f, 0.f, 0.f};
  glm::vec3 scale{1.f};
};

struct LocalToWorld {
  glm::mat4 value{1.f};
};

struct Camera {
  float fov_y = 1.04719755f;
  float z_near = 0.1f;
  float z_far = 10000.f;
  bool primary = false;
};

struct DirectionalLight {
  glm::vec3 direction{0.35f, 1.f, 0.4f};
  glm::vec3 color{1.f};
  float intensity = 1.f;
};

struct MeshRenderable {
  AssetId model;
};

struct SpriteRenderable {
  AssetId texture;
  glm::vec4 tint{1.f};
  int sorting_layer = 0;
};
```

Initial systems:

- Transform hierarchy/local-to-world calculation.
- Primary camera query.
- Render extraction.
- Optional FPS/editor camera controller as tooling, not core scene identity.

## Render Abstraction

The renderer abstraction should not be an RHI abstraction. The RHI already exists. This layer should abstract how scene data becomes render work.

Recommended flow:

```text
Flecs Scene
  -> Render extraction
  -> RenderScene snapshot
  -> IRenderer implementation
  -> RenderGraph/RHI
```

`RenderScene` should be renderer-neutral:

```cpp
struct RenderScene {
  std::vector<RenderCamera> cameras;
  std::vector<RenderDirectionalLight> directional_lights;
  std::vector<RenderMesh> meshes;
  std::vector<RenderSprite> sprites;
};
```

Renderer implementations choose what they consume:

- Meshlet renderer consumes cameras, lights, and meshes.
- Future 2D renderer consumes cameras and sprites.
- Future voxel/custom rendering can consume dedicated render entries or registered render extension data.

Avoid passing `RenderGraph` into gameplay/editor scenes. Only renderer implementations should add render graph passes.

Render service ownership direction:

- Put the engine-facing owner and frame boundary in `src/engine/render`.
- Keep low-level reusable renderer services under `src/gfx`, including `RenderGraph`, shader management, model GPU residency, RHI-backed helpers, and renderer internals.
- Avoid putting `SceneManager` or Flecs knowledge into `gfx`.
- `Engine` should own `RenderService` once renderer selection needs to be engine-wide, such as custom voxel or other specialized renderer implementations. Before that, a render layer may host the first service slice if it remains accessible to layers above it.
- The first diagnostic renderer for extracted data should be a simple clear/debug renderer, with optional `RenderScene` logging or inspection.

## Resource And Asset Direction

`ResourceManager` is currently a global singleton tied to `ModelGPUMgr`. That is practical for the current demos, but it is not a good final editor/runtime boundary.

Target direction:

- Engine owns an asset/resource service.
- Scene data stores stable `AssetId`s.
- Runtime resolves `AssetId` to loaded CPU asset data.
- Renderer resolves loaded assets to GPU residency.
- GPU handles are not serialized scene data.
- The long-term durable `AssetId` source is an asset registry with generated stable IDs. Source paths, importer type/version, import settings, and source content hashes are metadata on registry entries, not the identity itself.

Compatibility rule: keep `ResourceManager` as a temporary facade until the asset service (Phase 6) replaces it. The `demo_scene_compat` path in `DemoSceneEcsBridge` and preset code should stay the only place that turns `AssetId` + paths into loaded CPU/GPU model state for demos.

## Migration Scaffolding

This section names temporary structures so they do not accidentally become architecture.

### `TestApp`

Scaffolding role:

- Thin `vktest` executable over `Engine`.
- Smoke-test host.

Retire or shrink when:

- Another binary is the primary harness and `TestApp` has nothing left to own.

### `CompatibilityVktestLayer`

Scaffolding role:

- Wires `vktest`-specific demo behavior: `ResourceManager`, scene presets, hotkeys, transform sync for loaded models, ImGui for presets and `RenderService::on_imgui()`.

Retire when:

- Demos do not need global `ResourceManager` or preset hotkeys, or those move into a small game/editor module with explicit ownership.

### `DemoSceneEcsBridge` / `teng::gfx::demo_scene_compat`

Scaffolding role:

- Authors Flecs entities from `DemoScenePresetData`.
- Registers `AssetId` → path for demo resolution.
- Tracks per-scene `ModelHandle`s for loaded instanced models and syncs transforms from ECS.

Retire when:

- Engine asset service loads models; scene code never touches `ResourceManager` directly.

### `ResourceManager` Singleton

Scaffolding role:

- Existing model loading and bind to `ModelGPUMgr` for uploads.

Retire or wrap when:

- Engine asset service owns asset identity/loading.
- GPU residency is owned by a non-global service (or by `RenderService` in a way that is not a static singleton).
- Scene components store stable `AssetId`s; runtime `ModelHandle` stays out of scene data.

## Revised Migration Phases

### Phase 0: Planning And Guardrails

Deliverables:

- Keep this plan updated as architecture decisions change.
- Add short follow-up plans for Flecs submodule integration and engine layer skeleton before code changes.

Exit criteria:

- Agreement that data-first scenes, tick-first engine, layers, and early stable IDs are the foundation.

### Phase 1: Tick-Driven Engine And Layer Skeleton

Status: complete.

Delivered:

- Add `src/engine/Engine.hpp/.cpp`.
- Add `EngineConfig`, `EngineContext`, `EngineTime`.
- Add `Layer` and `LayerStack`.
- Move frame timing, resource/local-resource path setup, CVar lifecycle,
  window/device/swapchain ownership, and quit-after-frame handling into
  `Engine`.
- Add backend selection config with platform default, Vulkan, and Metal choices.
- Keep `vktest` behavior unchanged through `CompatibilityVktestLayer` in
  `apps/vktest/TestApp.cpp`.

Scaffolding kept (historical note for this phase; current tree may differ):

- `TestApp` as a thin executable compatibility host.
- Global `ResourceManager` inside compatibility wiring.

Exit criteria:

- `Engine::tick()` is public and can be driven manually.
- `Engine::run()` is implemented as a helper loop over `tick()`.
- `vktest --quit-after-frames 30` and `./scripts/agent_verify.sh` are the
  validation guardrails for this completed slice.

### Phase 2: Flecs Submodule And Data Scene Foundation

Status: complete.

Delivered:

- Add Flecs as `third_party/flecs` git submodule.
- Add CMake integration with static Flecs linked privately into `teng` while exposing Flecs headers publicly. This avoids duplicate Flecs runtime state across the current shared-library boundary.
- Add `engine::Scene` containing `flecs::world`.
- Register initial core components: stable entity GUID, name, transform, local-to-world, camera, directional light, mesh renderable, and sprite renderable.
- Add `SceneManager` with scene creation, lookup, active-scene tracking, and active-scene ticking.
- Add stable `SceneId`, `EntityGuid`, and `AssetId` types.
- Add basic flat transform/local-to-world system.
- Add `engine_scene_smoke` and include it in `./scripts/agent_verify.sh`.
- Expose `SceneManager` from `Engine` and `EngineContext`, and tick the active scene from `Engine::tick()`.

Scaffolding kept (historical note for this phase; current tree may differ):

- Demo scene data can be created by a temporary C++ demo loader or ECS authoring.
- Global `ResourceManager` where demos require it.

Exit criteria:

- A minimal Flecs scene can be created, ticked, and inspected/debug-logged.
- Entities have stable GUID components.
- Transform system updates `LocalToWorld`.
- `./scripts/agent_verify.sh` and `vktest --quit-after-frames 30` pass.

### Phase 3: RenderService And RenderScene Extraction

Status: complete for core goals; see Phase 5 for meshlet ownership.

Delivered:

- `RenderScene` snapshot types for cameras, lights, meshes, and sprites under `src/engine/render`.
- ECS render extraction from core components into `RenderScene`.
- Device-free extraction smoke coverage in `engine_scene_smoke`.
- `RenderService` owned by `Engine` / `EngineContext` with `ShaderManager`, `RenderGraph`, upload/copy, `ImGuiRenderer`, `ModelGPUMgr`, and active `IRenderer`.
- `IRenderer` and `RenderFrameContext`.
- `DebugClearRenderer` as a minimal diagnostic `IRenderer`.
- ImGui composition for the main window lives in `RenderService` (overlay pass), not in scene code.

Open follow-ups (not blocking “Phase 3” label):

- Optional `RenderScene` logging/inspection.
- Tighter ownership of `ModelGPUMgr` (still constructed in `RenderService` for all renderers; may move when multi-renderer or asset pipeline needs it).

Scaffolding still in use:

- `ResourceManager` for `vktest` demo loads via `demo_scene_compat`.

Exit criteria:

- Engine-owned `RenderService` extracts `RenderScene` from the active Flecs scene and invokes the active `IRenderer`.
- Data-scene render paths consume `RenderScene` without receiving `RenderGraph` from gameplay.
- Meshlet rendering runs through `gfx::MeshletRenderer` and `IRenderer` (Phase 5).

### Phase 4: Migrate Demo Presets Into ECS Data

Status: complete.

Deliverables:

- Convert current meshlet demo presets into entity creation against `engine::Scene`.
- Replace monolithic scene-owned model lists with ECS `MeshRenderable` plus `Transform` (the old `MeshletRendererTestScene` `models_` pattern is gone).
- Move camera and directional light state to ECS components.
- Add a temporary `AssetId` to source-path registry for demo resolution.
- Track runtime `ModelHandle` lifetime in `demo_scene_compat` / `DemoSceneEcsBridge` (loads on preset apply, not in `RenderScene`).

Scaffolding reduced:

- No meshlet “scene class” owns demo world data; Flecs + bridge code do.
- `ModelHandle` remains runtime compatibility state, not serialized scene state.
- Global `ResourceManager` remains behind demo compatibility code.

Exit criteria:

- All default meshlet demo presets are represented as Flecs entities.
- Reapplying presets clears previously authored demo entities.
- Transform updates flow from ECS to renderer extraction.
- Camera/light data comes from ECS for migrated presets.
- `engine_scene_smoke`, `./scripts/agent_verify.sh`, and `vktest --quit-after-frames 30` cover the bridge.

### Phase 5: Extract Meshlet Renderer Properly

Status: complete.

Delivered:

- `gfx::MeshletRenderer` (`src/gfx/renderer/MeshletRenderer.*`) implements `engine::IRenderer` and is the default renderer installed by `RenderService::init()`.
- Meshlet draw prep, depth pyramid, CSM, PSOs, visibility/readbacks, and final shade pass live in `src/gfx/renderer/` (promoted from the old `vktest` scene files).
- Consumes `RenderScene` (camera selection, directional light, output extent) and drives `ModelGPUMgr` through `RenderFrameContext`.
- Renderer-specific debug UI: `MeshletRenderer::on_imgui()`; preset list and app chrome remain in `CompatibilityVktestLayer`.
- The former `MeshletRendererTestScene` / `ITestScene` / `TestRenderer` types are removed; no `add_render_graph_passes` from scene code on the default path.

Residual debt (track under Phase 6 or small refactors):

- `ModelGPUMgr` is still owned by `RenderService` and passed via `RenderFrameContext` (convenient for a single meshlet renderer; revisit for multi-renderer or asset service).
- `ResourceManager` + `demo_scene_compat` still load CPU models for presets; that is expected until the asset service exists.

Exit criteria:

- Met: normal `vktest` does not use `ITestScene` or a meshlet test scene class for rendering.
- Met: active scene is Flecs data; `RenderService` presents via `MeshletRenderer`.

### Phase 6: Asset Service Boundary

Deliverables:

- Add engine-owned asset/resource service.
- Define stable asset registry format with generated durable asset IDs plus source path, importer metadata, import settings, and source content hash metadata.
- Resolve `AssetId` to CPU asset data.
- Let renderer service handle GPU residency for assets.
- Wrap or replace `ResourceManager` singleton.

Scaffolding retired or isolated:

- Direct runtime dependence on global `ResourceManager`.

Exit criteria:

- Scene components refer to stable `AssetId`s.
- Renderer code can acquire GPU resources without scene code storing GPU handles.

### Phase 7: 2D Runtime Path

Deliverables:

- Add minimal sprite components and extraction.
- Add simple 2D renderer or 2D path inside a renderer implementation.
- Support a small 2D sample scene.

Exit criteria:

- Engine can run a simple data-authored 2D scene.
- 2D support does not require meshlet renderer code.

### Phase 8: Editor Layer Foundation

Deliverables:

- Add build flag for editor layer.
- Add `EditorLayer` using ImGui.
- Add hierarchy view from Flecs entities.
- Add component inspector for registered core components.
- Add play/stop simulation model.
- Add scene reload flow.

Exit criteria:

- Editor runs on the same runtime.
- Editor can inspect and edit a data scene.
- Play mode can start/stop without restarting the app.

### Phase 9: Scripting Preparation

Deliverables:

- Define script component metadata shape.
- Define system scheduling points for script systems.
- Decide how Lua components map to Flecs/runtime component registration.
- Keep C++ native systems possible but not required for normal game authoring.

Exit criteria:

- Lua can be added without replacing scene, entity, component, or layer architecture.

## Completed Implementation Slices

### Slice 1: Tick-Driven Runtime Shell

The first implementation slice created the runtime control surface without
touching meshlet internals, Flecs, stable IDs, or renderer extraction.

1. `engine::EngineTime`.
2. `engine::EngineConfig`.
3. `engine::EngineContext`.
4. `engine::Layer` and `engine::LayerStack`.
5. `engine::Engine` with public `tick()` and `run()`.
6. `TestApp` as a compatibility host over `Engine`.
7. `CompatibilityVktestLayer` as the explicit bridge for `vktest` demo behavior and `RenderService` presentation.

### Slice 2: Flecs Scene Foundation

The second implementation slice added the first data-scene foundation without
migrating meshlet demo data or renderer ownership.

1. Flecs submodule and static CMake integration.
2. `engine::SceneId`, `engine::EntityGuid`, and `engine::AssetId`.
3. Core ECS components under `src/engine/scene`.
4. `engine::Scene` wrapping one `flecs::world`.
5. `engine::SceneManager` with active-scene access.
6. Active-scene ticking from `Engine::tick()`.
7. `engine_scene_smoke` coverage for scene creation, entity GUID lookup,
   transform-to-local-to-world updates, entity destroy, and normalized
   path-derived `AssetId`s.

### Slice 3: RenderScene Extraction And RenderService Shell

The third implementation slice added the renderer-facing scene bridge. Later
slices moved the default meshlet path onto `gfx::MeshletRenderer` (see Slice 5).

1. `engine::RenderScene` snapshot schema under `src/engine/render`.
2. ECS extraction from `Camera`, `DirectionalLight`, `MeshRenderable`,
   `SpriteRenderable`, `EntityGuidComponent`, and `LocalToWorld`.
3. Deterministic extraction ordering and invalid asset skip counters.
4. `SpriteRenderable::sorting_order`.
5. `engine::IRenderer` and `engine::RenderFrameContext`.
6. Engine-owned `engine::RenderService` exposed through `Engine` and
   `EngineContext`.
7. `engine::DebugClearRenderer` as the first minimal diagnostic renderer.
8. `engine_scene_smoke` coverage for render extraction.

### Slice 4: Demo Preset ECS Authoring And Resource Compatibility Bridge

The fourth implementation slice moved demo scene data into ECS authoring while
preserving meshlet demo visuals through `demo_scene_compat` and
`ResourceManager`.

1. `apps/common/ScenePresets.*` exposes `DemoScenePresetData` (camera defaults,
   optional CSM defaults, model source paths, per-instance transforms).
2. `apps/vktest/DemoSceneEcsBridge.*` authors camera, directional light, mesh
   renderable, transform, local-to-world, name, and stable GUID components into
   the active `engine::Scene`.
3. Preset reapplication clears previously authored demo entities by tracked
   `EntityGuid`.
4. `demo_scene_compat` registers `AssetId` → path, loads instanced models via
   `ResourceManager`, and tracks per-scene `ModelHandle`s; `sync_loaded_model_transforms`
   updates transforms from ECS.
5. `engine_scene_smoke` covers procedural demo authoring, valid asset IDs,
   deterministic extraction, and stale entity cleanup.

### Slice 5: Meshlet `IRenderer` Extraction

The fifth slice removed the old `TestRenderer` / `ITestScene` / meshlet test
scene path and made `gfx::MeshletRenderer` the real `IRenderer` for the default
`vktest` run.

1. `src/gfx/renderer/MeshletRenderer.*` — meshlet PSOs, CSM, depth pyramid, draw
   prep, shade pass, stats readbacks, `on_imgui` for renderer diagnostics.
2. `RenderService` constructs `std::make_unique<gfx::MeshletRenderer>()` at init.
3. Meshlet helpers live under `src/gfx/renderer/` (e.g. `MeshletDrawPrep`,
   `MeshletDepthPyramid`, `MeshletCsmRenderer`, `MeshletTestRenderUtil`).
4. `CompatibilityVktestLayer` calls `ctx.renderer().enqueue_active_scene()` and
   `ctx.renderer().on_imgui()`; no scene-owned render graph.
5. Validation: `./scripts/agent_verify.sh` and
   `./build/Debug/bin/vktest --quit-after-frames 30`.

Current next implementation work: Phase 6 asset/resource service, then Phase 7 2D
path. `ResourceManager` and `demo_scene_compat` remain until then.

## Validation Strategy

Required command after implementation slices:

```bash
./scripts/agent_verify.sh
```

Smoke test:

```bash
./build/Debug/bin/vktest --quit-after-frames 30
```

For shader or shared HLSL changes, rely on `agent_verify.sh` because it runs `teng-shaderc --all`.

## Open Follow-Up Design Notes

These should become small plans before the related implementation phase. Current
triage after the first Phase 3 slice:

1. Flecs submodule/CMake integration plan: done. Covered by `plans/flecs_scene_foundation_design.md` and the completed Phase 2 implementation. No new doc needed unless the Flecs version/pin policy changes.
2. Stable ID and asset handle design note: partially done. `SceneId`, `EntityGuid`, and path-derived `AssetId` exist. The long-term answer is an asset registry with generated durable IDs plus source/import metadata; fold the detailed registry/resource-service design into the asset service boundary work before Phase 6.
3. Editor play-mode semantics: not needed yet. Write this before Phase 8, after runtime scenes and renderer extraction are stable for editor use.
4. RenderScene schema for 2D plus 3D: first implementation done for Phase 3 in `src/engine/render/RenderScene.hpp`. The companion note `plans/render_service_extraction_design.md` is outdated (it still references removed `TestRenderer` types); refresh it when editing renderer docs.
5. Asset service boundary and `ResourceManager` retirement plan: next major doc/code milestone. Meshlet `IRenderer` extraction is done; `ModelGPUMgr` + `ResourceManager` ownership should be revisited as part of Phase 6. The `AssetId -> path -> ResourceManager` bridge in `demo_scene_compat` is temporary compatibility code.
6. Metal validation plan for `EngineConfig` backend selection: useful but not blocking Phase 3. Write before backend-specific renderer/service changes or before any migration slice that claims Metal parity.
