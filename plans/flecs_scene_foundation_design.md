# Flecs Scene Foundation Design

Status: complete. Phase 2 implemented the initial Flecs scene foundation; this note now records the intended design and what landed.

Scope: integrate Flecs as the engine scene ECS foundation, introduce stable scene/entity/asset identifiers, define the first `Scene` and `SceneManager` responsibilities, and provide a migration path away from C++ gameplay scene subclasses. This note is specific to the current `metalrender` layout and should be read with `plans/engine_runtime_migration_plan.md`.

## Implemented Phase 2 Slice

Delivered:

- `third_party/flecs` submodule added and wired through `third_party/CMakeLists.txt`.
- Flecs is built static-only and linked privately into the shared `teng` library while Flecs headers are exposed for public engine scene APIs. This avoids duplicate Flecs runtime state in app executables.
- `src/engine/scene` now contains stable ID types, core scene components, `Scene`, `SceneManager`, and scene smoke coverage.
- `Engine` owns `SceneManager`, exposes it through `Engine::scenes()` and `EngineContext::scenes()`, and ticks the active scene from `Engine::tick()`.
- `engine_scene_smoke` validates scene creation, GUID lookup, transform-to-`LocalToWorld`, entity destruction, and normalized path-derived `AssetId`s.
- `./scripts/agent_verify.sh` builds and runs `engine_scene_smoke`.

Deferred:

- Transform hierarchy/parent relationships.
- Scene serialization.
- Demo preset conversion into ECS entities is now implemented as vktest compatibility authoring.
- Resource bridge from `MeshRenderable{AssetId}` to `ResourceManager`/`ModelGPUMgr` is now implemented as renderer/resource compatibility code in `TestRenderer`.
- Renderer-neutral `RenderScene` extraction is now implemented under `src/engine/render`.
- Editor component metadata and play/edit world semantics.

## Relevant Current Code

- `third_party/CMakeLists.txt` owns third-party integration directly with `add_subdirectory(...)` and cache option setup before dependencies are added. Existing third-party dependencies are recorded in `.gitmodules` under `third_party/<name>`.
- `src/CMakeLists.txt` builds the shared `teng` library from an explicit source list and links public dependencies such as `glm::glm`, `meshoptimizer`, `ktx`, `concurrentqueue`, `implot`, and the project warning target.
- `apps/vktest/TestApp.cpp` currently owns runtime bootstrap: resource directory setup, CVar load/save, window/device/swapchain creation, `gfx::TestRenderer` creation, global `ResourceManager` init/shutdown, input callbacks, ImGui frame setup, and the main loop.
- `apps/vktest/TestRenderer.*` currently owns renderer services plus debug scene selection. It constructs `ModelGPUMgr`, `RenderGraph`, ImGui renderer, upload/copy helpers, static instance data, material storage, and the active `ITestScene`.
- `apps/vktest/TestDebugScenes.hpp` defines `ITestScene`, a C++ virtual scene interface whose methods receive `TestSceneContext` and can add render graph passes directly.
- `apps/vktest/scenes/MeshletRendererTestScene.*` is the main working meshlet demo. It still mixes camera/controller tooling, light/day-night controls, render graph pass wiring, CSM, depth pyramid, draw prep, and debug ImGui, but preset world/model data now comes from ECS authoring.
- `apps/common/ScenePresets.*` provides data-first demo presets as plain camera defaults, optional CSM defaults, source model paths, and per-instance transforms. The older callback-based loader wrapper remains for compatibility.
- `apps/vktest/DemoSceneEcsBridge.*` converts demo presets into Flecs entities and owns the temporary demo `AssetId` to source-path registry.
- `src/core/Handle.hpp` and `src/core/Pool.hpp` provide runtime generational handles. They are good for in-process object lifetime, but they are not stable serialized IDs.
- `src/ResourceManager.*` is a global singleton that caches models by `std::hash<std::string>` of the path, stores CPU `ModelInstance` data, creates per-instance GPU handles through `ModelGPUMgr`, and returns runtime `ModelHandle`s.
- `src/gfx/ModelGPUManager.*` owns model GPU residency and per-instance GPU allocation through `ModelGPUHandle` and `ModelInstanceGPUHandle`. It loads model files, uploads model resources, allocates instance data, and frees GPU resources.

## Target Architecture

The target scene model is data-first:

```text
Engine
  owns SceneManager
  ticks layers and scene systems

SceneManager
  owns loaded Scene objects
  tracks active/edit/play scenes
  handles load/unload/reload/play transitions

Scene
  owns one flecs::world
  stores a stable SceneId
  registers engine component modules
  contains entities with EntityGuid components

Asset/Resource service
  maps stable AssetId values to source assets and loaded CPU resources
  resolves renderable components into runtime resources

RenderService
  extracts renderer-neutral data from Flecs
  resolves AssetId data to runtime/GPU handles through asset/resource services
  keeps RenderGraph and backend details outside gameplay scene data
```

`Scene` is not a polymorphic gameplay base class. It should not have virtual game update/render methods, and new game or demo behavior should not be authored by subclassing `Scene`. Behavior should enter through registered ECS systems, tooling systems, compatibility loaders, or future script systems.

## Flecs Submodule Integration

Flecs has been added as a git submodule at `third_party/flecs` and recorded in `.gitmodules`, matching the repo's current dependency pattern.

Implemented CMake shape:

- Flecs setup lives in `third_party/CMakeLists.txt` near other general-purpose libraries, after Flecs cache options are set.
- The engine library links against `flecs::flecs_static`; Flecs sources are not vendored into `src/CMakeLists.txt`.
- Flecs is owned by `teng`, not by `vktest`, so engine scene code is available to future app/editor targets.
- Avoid adding Vulkan, Metal, renderer, or app-specific compile definitions to Flecs integration.
- Keep warning treatment for third-party code consistent with the surrounding file. If Flecs emits warnings under `project_warnings`, isolate it as a third-party target instead of weakening warnings on engine code.

Implementation note: `teng` currently builds as a shared library. Flecs is linked privately into `teng` while `third_party/flecs/include` is exposed publicly, because public static linkage caused app executables to instantiate separate Flecs runtime state when using inline C++ APIs.

The implementation does not add a custom wrapper that hides Flecs. Engine code exposes `flecs::world&` from `Scene` while still providing stable-ID helpers for creation, lookup, and serialization boundaries.

## IDs And Handles

Introduce stable identifiers separately from existing runtime handles:

- `SceneId`: stable identity for a scene asset or generated scene. This belongs to `Scene` and is used by `SceneManager`.
- `EntityGuid`: stable identity stored as an ECS component on every persistent scene entity.
- `AssetId`: stable identity for source assets referenced by scene data.

Do not reuse `GenerationalHandle<T>` for these IDs. Existing handles are runtime pool references with generation bits; they are not appropriate for saved scene files, editor references, or cross-run identity.

Do not serialize `ModelHandle`, `ModelGPUHandle`, `ModelInstanceGPUHandle`, `rhi::*Handle`, or `RGResourceId` into scene data. Those remain runtime or frame-local identities.

Phase 2 implemented small strongly typed `uint64_t` values for each ID. `SceneId` and `EntityGuid` use a monotonic runtime generator. The final generation policy can evolve, but the type boundary now exists before scene/render/resource code starts depending on it.

`AssetId` initially supports deterministic mapping from normalized source paths through a fixed FNV-1a hash, because current model identity is effectively path-based through `ResourceManager`. The plan still leaves room for a later asset registry file that stores canonical IDs, import metadata, labels, and source paths.

## Core Scene Components

Register the first engine-owned component module once per scene world:

- `EntityGuidComponent { EntityGuid guid; }`
- `Name { std::string value; }`
- `Transform { translation, rotation, scale }`
- `LocalToWorld { glm::mat4 value; }`
- `Camera { projection settings, clipping planes, primary flag }`
- `DirectionalLight { direction or transform-derived direction, color, intensity }`
- `MeshRenderable { AssetId model; }`
- `SpriteRenderable { AssetId texture, tint, sorting layer }`

Deferred component:

- `Parent` or a relationship-based parent model using Flecs relationships.

The exact component names can change during implementation, but these responsibilities should stay separate:

- Stable identity is a component, not the Flecs entity ID.
- Authoring transform and derived world transform are separate.
- Renderable components store stable `AssetId`s, not `ModelHandle`s.
- Renderer-specific GPU state is not stored on the authoring entity.

Component registration should live in engine scene code, not in `apps/vktest`. A future game/editor module may register additional components, but engine components should be available to all runtime targets.

## Scene

Initial `Scene` responsibilities:

- Own a `flecs::world`.
- Store its `SceneId` and optional human-readable name.
- Register engine component modules and engine scene systems.
- Create entities with an `EntityGuidComponent`.
- Maintain a lookup from `EntityGuid` to `flecs::entity`, rebuilt or validated as needed.
- Provide helpers to create, destroy, and find entities by stable GUID.
- Expose the world for systems, editor inspection, and queries.

Initial `Scene` non-responsibilities:

- It does not own the platform window, RHI device, swapchain, or render graph.
- It does not own model GPU residency.
- It does not subclass into game-specific C++ scene classes.
- It does not contain demo-specific input handling like the current FPS camera controller.
- It does not directly submit render passes.

The first scene can be created procedurally by a compatibility/demo loader. That loader should produce Flecs entities and components, not a new `Scene` subclass.

## SceneManager

Initial `SceneManager` responsibilities:

- Create and destroy scenes.
- Track an active runtime scene.
- Provide hooks for edit/play scene switching once the editor layer exists.
- Own scene load/unload/reload sequencing.
- Keep scene lifetime independent from `vktest` debug scene switching.
- Provide a narrow API for engine layers to access the active `Scene` and its `flecs::world`.

The first implementation can manage one active scene plus a small map by `SceneId`. Do not overbuild multi-scene streaming yet. The important early boundary is that the engine owns scene lifetime and scenes are data worlds.

Future editor/play-mode support should allow:

- Edit world remains the source of truth.
- Play world can be cloned or loaded from serialized data.
- Stop play discards runtime mutations unless explicitly applied.
- Runtime-only components can be excluded from serialized data.

## Component Registration

Use an explicit registration pass rather than relying on incidental template use from scattered systems.

Recommended direction:

- Add an engine scene/component registration function or module that receives `flecs::world&`.
- Register core components, relationships, and systems in a deterministic order during `Scene` construction.
- Keep render extraction registration separate from authoring component registration, so tests and tools can create scenes without installing a renderer.
- Allow future native game modules to register additional components and systems through a runtime/module hook.
- Store enough metadata for editor inspection later: display name, category, serializable/runtime-only flags, and field metadata where practical.

Avoid making the first implementation a closed enum of component types. The editor and future scripting path need dynamic registration and discovery.

## AssetId And Resource Boundary

Scene components should reference assets using `AssetId`. Runtime resource loading should resolve those IDs into loaded CPU resources and then into GPU residency as needed.

Initial bridge from current code:

- Treat `ResourceManager` and `ModelGPUMgr` as compatibility services.
- Add a resolver concept that maps `AssetId` to a source model path, then uses the existing model loading path while meshlet rendering is being migrated.
- Keep `ModelHandle` only inside runtime compatibility/resource layers.
- Keep `ModelGPUHandle` and `ModelInstanceGPUHandle` inside renderer/resource residency code.

Longer-term asset service responsibilities:

- Store canonical `AssetId` to source path/import metadata mapping.
- Load CPU asset data independent of a renderer backend.
- Let renderer services decide GPU residency and per-instance allocation.
- Support non-model assets such as textures, materials, scripts, scenes, and sprite resources.

The current `ResourceManager` path hash is not enough as final asset identity. `std::hash<std::string>` is implementation-defined and not a durable asset ID contract.

## Avoiding Gameplay C++ Scene Subclasses

Do not add a new hierarchy such as `class Scene { virtual update(); virtual render(); }` or `class MeshletScene : public Scene`.

Allowed temporary adapters:

- A demo scene loader that converts `apps/common/ScenePresets` into Flecs entities.
- A compatibility layer that keeps `ITestScene` running while the engine runtime and renderer boundary are introduced.
- Tooling systems such as an editor/FPS camera controller, as long as they are systems/components or layers, not scene identity.

Not allowed as destination architecture:

- A Flecs world hidden behind game-specific C++ scene subclasses.
- Gameplay scenes adding render graph passes directly.
- Scene components storing GPU handles as authored state.
- New engine runtime APIs depending on `apps/vktest` scene types.

## Migration Scaffolding And Retirement Criteria

`apps/vktest/TestApp.*`

- Scaffolding role: current executable shell and bootstrap owner.
- Retire when `main.cpp` or a thin app wrapper can construct `engine::Engine`, install layers, and call `run()` or drive `tick()` without owning resources/rendering directly.

`apps/vktest/TestRenderer.*`

- Scaffolding role: current renderer service bundle and debug scene router.
- Retire from engine runtime when `RenderService` owns shared renderer frame services and active renderers are selected through engine/runtime APIs.

`apps/vktest/TestDebugScenes.*` and `ITestScene`

- Scaffolding role: legacy graphics test harness.
- Retire from runtime scene architecture when demos load into Flecs scenes and no engine scene path depends on `ITestScene`. It may remain in a separate graphics test app if useful.

`apps/vktest/scenes/MeshletRendererTestScene.*`

- Scaffolding role: working reference for meshlet renderer behavior, demo presets, CSM, depth pyramid, draw prep, camera, light, and debug UI.
- Retire when model/camera/light data comes from ECS, render extraction feeds a renderer-neutral snapshot, and meshlet-specific pass wiring lives in a meshlet renderer implementation.

Global `ResourceManager`

- Scaffolding role: model path cache plus CPU/GPU model instance bridge.
- Retire or wrap when an engine asset/resource service owns stable asset identity and CPU loading, while renderer services own GPU residency.

`ModelGPUMgr`

- Scaffolding status: not inherently temporary, but its current ownership through `TestRenderer` and `ResourceManager` is temporary.
- Keep as renderer residency machinery only if its API is made independent of scene identity and fed by render/resource services rather than authored scene classes.

## Phased Implementation Sequence

### Phase 0: Dependency And Design Guardrails

Status: complete.

Deliverables:

- Add Flecs as `third_party/flecs` submodule.
- Wire Flecs into `third_party/CMakeLists.txt` and link `teng` to the C++ Flecs target.
- Add a small engine scene source area to `src/CMakeLists.txt` for scene foundation files.
- Keep `vktest` behavior unchanged.

Exit criteria:

- `./scripts/agent_verify.sh` passes.
- No new app or renderer code depends on Flecs yet unless part of a tiny compile/link smoke test.

### Phase 1: Stable ID Types

Status: complete.

Deliverables:

- Add strongly typed `SceneId`, `EntityGuid`, and `AssetId`.
- Add invalid/null values and basic comparisons/hash support.
- Add a deterministic initial ID generation policy and document its limits.

Exit criteria:

- Stable IDs are distinct from existing `GenerationalHandle<T>` runtime handles.
- No scene-facing component or API requires `ModelHandle` for authored identity.

### Phase 2: Scene World Foundation

Status: complete.

Deliverables:

- Add `Scene` that owns `flecs::world`.
- Register core engine components.
- Add entity creation/destruction/lookup by `EntityGuid`.
- Add basic transform-to-world system.
- Add a minimal procedural scene creation path for smoke testing.

Exit criteria:

- A minimal scene can be created and ticked without `ITestScene`.
- Entities have stable GUID components.
- `LocalToWorld` updates from `Transform`.

### Phase 3: SceneManager

Status: complete.

Deliverables:

- Add `SceneManager` with loaded scene storage and active scene tracking.
- Provide create/destroy/set-active APIs.
- Add engine-context access points for the active scene.
- Keep the API ready for future edit/play worlds without implementing the full editor flow.

Exit criteria:

- Engine/runtime code can access the active Flecs scene through `SceneManager`.
- Scene lifetime is not owned by `TestRenderer`.

### Phase 4: Demo Preset Compatibility Loader

Status: complete.

Deliverables:

- Add a compatibility loader that converts `apps/common/ScenePresets`-style model paths and transforms into Flecs entities with `Transform` and `MeshRenderable`.
- Map source model paths to temporary `AssetId`s.
- Keep the loader clearly named as demo/compatibility code.

Exit criteria:

- Sponza, Suzanne, chessboard, grid, random, and mixed default presets can exist as ECS data.
- The loader does not create a new C++ scene subclass.
- The loader does not expose `RenderGraph` or GPU handles to scene data.

### Phase 5: Resource Bridge For Mesh Renderables

Status: complete as compatibility scaffolding.

Deliverables:

- Add an adapter that resolves `MeshRenderable{AssetId}` through a temporary asset map into the current `ResourceManager`/`ModelGPUMgr` flow.
- Keep runtime `ModelHandle`s in a bridge-owned cache keyed by entity GUID or instance identity.
- Define cleanup when entities or scenes unload.

Exit criteria:

- Current meshlet demo assets can be represented as ECS renderables while rendering still uses existing model GPU machinery.
- Resource bridge ownership is explicit and can be deleted later.

### Phase 6: Render Extraction Preparation

Status: complete. This moved into the Phase 3 renderer migration work and is tracked in `plans/render_service_extraction_design.md`.

Deliverables:

- Define ECS queries for cameras, directional lights, mesh renderables, and sprite renderables.
- Produce a renderer-neutral render snapshot or intermediate data structure.
- Keep render graph pass creation in renderer services, not in `Scene`.

Exit criteria:

- Scene data can feed renderer extraction without `ITestScene::add_render_graph_passes`.
- `MeshletRendererTestScene` has a clear split point for further migration.

## Risks And Tradeoffs

- Flecs CMake target names/options may not match assumptions. Check the submodule's CMake exports before wiring it into `teng`.
- Introducing Flecs directly into public engine headers increases compile exposure. This is acceptable for the first scene foundation, but avoid spreading Flecs into low-level renderer/RHI APIs.
- Stable ID generation is easy to under-specify. A local `uint64_t` generator is acceptable only if the plan distinguishes temporary generated IDs from imported asset registry IDs.
- `std::hash<std::string>` path hashes in `ResourceManager` are not stable across implementations and should not leak into `AssetId`.
- Current model loading creates GPU instances during resource loading. Bridging that into ECS can accidentally make scene data own GPU residency unless the bridge boundary is explicit.
- Transform hierarchy choices matter. Flecs relationships are attractive, but the first transform system should be simple enough to validate and replace if editor requirements force a richer hierarchy model.
- Keeping `ITestScene` alive during migration is useful for validation, but new runtime code can accidentally depend on it. Compatibility code should be named and isolated.
- The first renderer extraction pass may reveal that `ModelInstance` contains both source model data and per-instance state. Avoid solving the whole asset pipeline before the ECS scene boundary exists.

## Validation Strategy

Completed implementation validation:

- `./scripts/agent_verify.sh --format`
- `./scripts/agent_verify.sh`
- `cmake --build build/Debug --target vktest && ./build/Debug/bin/vktest --quit-after-frames 30`

Validation after future code phases:

- Run `./scripts/agent_verify.sh` from repo root after each implementation slice.
- Run `./build/Debug/bin/vktest --quit-after-frames 30` after runtime/app integration slices.
- Add focused unit or smoke coverage for `EntityGuid` lookup, scene create/destroy, component registration, and transform system behavior when a test harness exists.
- For resource bridge phases, load and unload a scene repeatedly and verify model instance counts return to baseline.
- For render extraction phases, validate that scene code does not include `RenderGraph` and authored components do not store runtime GPU handles.

## Open Questions

- Which Flecs version or branch should the submodule pin to long term?
- When should `SceneId` and `EntityGuid` move from monotonic runtime IDs to imported/persistent IDs?
- Where should the first asset registry live: under `resources/`, under a future project directory, or generated into `resources/local` for experiments?
- Should transform hierarchy use Flecs relationships immediately, or start with an explicit parent component and migrate once editor requirements are clearer?
- How much editor metadata should be included in the first component registration pass?
- Should demo preset conversion live under `apps/common`, `apps/vktest`, or an engine compatibility namespace?
- What is the first non-mesh renderable target: sprites for 2D games, debug primitives, or UI/editor gizmos?
- Should scene serialization be deferred entirely until after render extraction, or should a minimal text/binary format be introduced as soon as stable IDs exist?
