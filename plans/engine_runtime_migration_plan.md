# Engine Runtime Migration Plan

Status: Phase 1 runtime shell implemented; later Flecs, render service, asset, and editor phases remain planned.

This plan treats the engine as a long-term runtime/editor foundation, not as a cleaned-up version of the current demo harness. The current `vktest`, `TestRenderer`, `ITestScene`, and `MeshletRendererScene` code should keep working during migration, but they are scaffolding. The destination is a tick-driven, layer-composed engine with data-first Flecs scenes, stable IDs, renderer services, and an editor layer on top of the same runtime.

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

`apps/vktest/TestApp.cpp` currently owns process and runtime work:

- Resource directory discovery and working directory setup.
- CVar load/save.
- Window creation.
- RHI device and swapchain creation.
- `gfx::TestRenderer` creation.
- Global `ResourceManager` init/shutdown.
- Main loop.
- ImGui frame lifecycle.
- Top-level debug key bindings.

Long term, most of this belongs in `engine::Engine` and layers. `TestApp` should become temporary scaffolding, then either disappear or become a thin test executable wrapper.

### `TestRenderer`

`apps/vktest/TestRenderer.cpp` currently owns renderer services and debug-scene orchestration:

- Shader manager.
- Render graph.
- ImGui renderer.
- Frame upload allocator and buffer copy manager.
- Model GPU manager.
- Static instance and geometry batches.
- Material buffer and samplers.
- Debug scene creation/switching.
- Frame timing.
- Render graph bake/execute.
- Swapchain acquire/submit.
- Scene input forwarding.

Long term, renderer services belong in `engine::RenderService` or lower-level `gfx` services. Debug-scene orchestration should remain test-only scaffolding.

### `MeshletRendererScene`

`apps/vktest/scenes/MeshletRendererTestScene.cpp` currently mixes:

- Demo scene data.
- Model instance lifetime.
- Camera state.
- Light/day-night state.
- Transform synchronization.
- Meshlet draw prep.
- Depth pyramid.
- CSM.
- Render graph pass wiring.
- Debug ImGui.

Long term, scene data belongs in Flecs, render extraction belongs in the engine render bridge, and meshlet-specific passes belong in a meshlet renderer implementation. This class should be split gradually, then deleted.

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
- `CompatibilityVktestLayer`: temporary layer that forwards to existing `TestRenderer`/`ITestScene` while systems are moved.

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

## Resource And Asset Direction

`ResourceManager` is currently a global singleton tied to `ModelGPUMgr`. That is practical for the current demos, but it is not a good final editor/runtime boundary.

Target direction:

- Engine owns an asset/resource service.
- Scene data stores stable `AssetId`s.
- Runtime resolves `AssetId` to loaded CPU asset data.
- Renderer resolves loaded assets to GPU residency.
- GPU handles are not serialized scene data.

Compatibility rule: keep `ResourceManager` as a temporary facade until meshlet rendering is migrated. Mark call sites that are expected to disappear.

## Migration Scaffolding

This section names temporary structures so they do not accidentally become architecture.

### `TestApp`

Scaffolding role:

- Existing executable shell.
- Smoke-test host.
- Temporary owner of runtime bootstrap until `Engine` replaces it.

Retire when:

- `main.cpp` can create `engine::Engine` directly or `TestApp` is only a thin wrapper with no renderer/resource ownership.

### `gfx::TestRenderer`

Scaffolding role:

- Compatibility renderer service for the existing debug scenes.
- Source to extract `RenderService` responsibilities from.

Retire when:

- `engine::RenderService` owns shader/render graph/upload/model GPU services.
- Active renderer is selected through `IRenderer`.
- Debug scene switching no longer depends on `TestRenderer`.

### `ITestScene` And `TestDebugScene`

Scaffolding role:

- Legacy renderer/debug test harness.
- Useful for keeping small graphics experiments alive while runtime scenes mature.

Retire from runtime path when:

- Engine scenes are Flecs data scenes.
- Demo presets load into ECS.
- Renderer tests can live in a separate test/debug app path.

They may remain in a graphics test executable, but they should not define engine scene architecture.

### `MeshletRendererScene`

Scaffolding role:

- Working reference implementation for meshlet rendering behavior.
- Temporary bridge for CSM, depth pyramid, draw prep, camera, light, and model preset behavior.

Retire when:

- Meshlet-specific render code lives in `MeshletRenderer`.
- Camera/light/model data comes from ECS and `RenderScene`.
- Demo presets instantiate Flecs entities.
- Debug UI is split into scene data UI and meshlet renderer debug UI.

### `ResourceManager` Singleton

Scaffolding role:

- Existing model loading and GPU upload bridge.

Retire or wrap when:

- Engine asset service owns asset identity/loading.
- Renderer service owns GPU residency.
- Scene components store stable asset IDs rather than `ModelHandle`s.

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

Scaffolding kept:

- `TestApp` as a thin executable compatibility host.
- `TestRenderer`.
- `ITestScene`.
- `MeshletRendererScene`.
- Global `ResourceManager` inside compatibility wiring.

Exit criteria:

- `Engine::tick()` is public and can be driven manually.
- `Engine::run()` is implemented as a helper loop over `tick()`.
- `vktest --quit-after-frames 30` and `./scripts/agent_verify.sh` are the
  validation guardrails for this completed slice.

### Phase 2: Flecs Submodule And Data Scene Foundation

Deliverables:

- Add Flecs as `third_party/flecs` git submodule.
- Add CMake integration.
- Add `engine::Scene` containing `flecs::world`.
- Register initial core components.
- Add `SceneManager`.
- Add stable `SceneId`, `EntityGuid`, and `AssetId` types.
- Add basic transform/local-to-world system.

Scaffolding kept:

- Existing debug scenes still render through compatibility path.
- Demo scene data can initially be created by a temporary C++ demo loader.

Exit criteria:

- A minimal Flecs scene can be created, ticked, and inspected/debug-logged.
- Entities have stable GUID components.
- Transform system updates `LocalToWorld`.

### Phase 3: RenderService And RenderScene Extraction

Deliverables:

- Add `RenderService`.
- Add `RenderScene` snapshot types for cameras, lights, meshes, and sprites.
- Add ECS render extraction from core components into `RenderScene`.
- Move renderer-owned frame services out of `TestRenderer` where practical.
- Add `IRenderer` interface.
- Add a compatibility renderer implementation that can still delegate to current `TestRenderer` or meshlet scene code while extraction matures.

Scaffolding kept:

- `MeshletRendererScene` may still produce final render passes.
- `ResourceManager` may still load models.

Exit criteria:

- Engine frame flow is `tick layers -> update scene -> extract RenderScene -> render`.
- Gameplay/data scenes do not receive `RenderGraph`.
- Existing meshlet demo remains functional.

### Phase 4: Migrate Demo Presets Into ECS Data

Deliverables:

- Convert current meshlet demo presets into entity creation against `engine::Scene`.
- Replace `MeshletRendererScene::models_` ownership with ECS `MeshRenderable` plus `Transform`.
- Move camera and directional light state to ECS components.
- Keep FPS camera as a controller/tooling system operating on a camera entity.

Scaffolding reduced:

- `MeshletRendererScene` should stop owning world data.
- `ModelHandle` should become runtime resource state, not scene state.

Exit criteria:

- At least one current meshlet preset is represented as Flecs entities.
- Transform updates flow from ECS to renderer extraction.
- Camera/light data comes from ECS for the migrated preset.

### Phase 5: Extract Meshlet Renderer Properly

Deliverables:

- Create `MeshletRenderer` implementing `IRenderer`.
- Move CSM, depth pyramid, draw prep, and meshlet pass wiring into renderer-owned code.
- Consume `RenderScene` mesh/camera/light data.
- Split debug UI into renderer debug UI and scene/editor UI.

Scaffolding retired:

- `MeshletRendererScene` should be deleted or reduced to a test-only adapter.

Exit criteria:

- Meshlet rendering no longer depends on `ITestScene`.
- Active runtime scene is Flecs data.
- Meshlet renderer can be selected as an `IRenderer`.

### Phase 6: Asset Service Boundary

Deliverables:

- Add engine-owned asset/resource service.
- Define stable asset registry format or temporary registry file.
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

## Completed First Code Slice

The first implementation slice created the runtime control surface without
touching meshlet internals, Flecs, stable IDs, or renderer extraction:

1. `engine::EngineTime`.
2. `engine::EngineConfig`.
3. `engine::EngineContext`.
4. `engine::Layer` and `engine::LayerStack`.
5. `engine::Engine` with public `tick()` and `run()`.
6. `TestApp` as a compatibility host over `Engine`.
7. `CompatibilityVktestLayer` as the explicit bridge to current debug rendering.

Next implementation slice: start the Flecs scene foundation or, if smaller
runtime cleanup is preferred first, move `CompatibilityVktestLayer` into a named
app source/header and add focused smoke coverage for manual `Engine::tick()`.

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

These should become small plans before the related implementation phase:

1. Flecs submodule/CMake integration plan.
2. Stable ID and asset handle design note.
3. Editor play-mode semantics on top of the implemented layer/tick order.
4. RenderScene schema for 2D plus 3D.
5. Asset service boundary and `ResourceManager` retirement plan.
6. Metal validation plan for the implemented `EngineConfig` backend selection.
