# Asset Registry And Runtime Asset Service Plan

Status: investigation and requirements for Phase 6 of `plans/engine_runtime_migration_plan.md`.

Scope: define the long-term asset registry, asset dependency, CPU resource, and renderer GPU residency boundaries for `metalrender`. This plan intentionally avoids implementation code. It is the design target for replacing the current `ResourceManager` singleton and making scenes durable data that reference assets by stable IDs.

## Current Code

Relevant current paths:

- `src/engine/scene/SceneIds.*`: defines `AssetId` as a `uint64_t`; `AssetId::from_path()` is a deterministic FNV-1a hash of a normalized path. This is useful as migration scaffolding only.
- `src/engine/scene/SceneComponents.hpp`: renderable scene data stores `AssetId`, not runtime handles.
- `src/engine/render/RenderScene.*`: extracted render data carries `AssetId` for meshes and sprites.
- `apps/vktest/DemoSceneEcsBridge.*`: temporary demo bridge that maps `AssetId -> filesystem path`, loads models through global `ResourceManager`, stores runtime `ModelHandle`s by scene/entity, and syncs ECS transforms into loaded `ModelInstance`s.
- `src/ResourceManager.*`: global singleton that hashes paths with `std::hash<std::string>`, loads CPU model data, caches model GPU resources, allocates per-instance runtime handles, frees GPU instances, and owns lifetime counters.
- `src/gfx/ModelGPUManager.*`: renderer-side model residency machinery. It currently also calls `gfx::load_model()` from `ModelLoader`, so CPU import/loading and GPU upload are coupled.
- `src/engine/render/RenderService.*`: owns `ShaderManager`, `RenderGraph`, upload helpers, `ImGuiRenderer`, and `ModelGPUMgr`; exposes `model_gpu_mgr()` so compatibility code can initialize `ResourceManager`.

The important current split is already partially correct: scenes and render extraction use `AssetId`, while runtime/GPU handles stay outside scene data. The incomplete part is that `AssetId` is still path-derived, the registry does not exist, CPU asset loading is not an engine service, and model GPU residency is still reached through `ResourceManager`/`RenderService` compatibility seams.

## Lessons From Existing Engines

Unity keeps stable asset identity separate from paths by assigning IDs and storing import metadata beside assets in `.meta` files. Unity's documentation is explicit that those IDs let assets move or rename without breaking references, and that losing metadata breaks references.

Godot 4.4 has a project `ResourceUID` service. Its UIDs keep references intact when resources are moved or renamed, and the service maps IDs to paths.

Unreal uses an asset registry and redirectors. When assets are moved or renamed, redirectors let unloaded packages still find the target until references are fixed up. Unreal also has documented dangling redirector failure modes, which is the warning for this project: redirects and deletion policy must be first-class, not incidental.

The shared pattern is not "store paths in scenes". The shared pattern is stable asset identity, separate import metadata, an index/query service, and explicit behavior for moves, renames, deletes, missing assets, and redirects.

References used for this investigation:

- Unity Manual, Asset Metadata: stable IDs, `.meta` files, import settings, and broken references when metadata is lost.
- Unity Addressables, Asset References: typed asset references and GUID-backed catalog references.
- Godot 4.4 `ResourceUID`: project resource IDs that keep references valid across moves/renames.
- Unreal Engine Asset Redirectors: redirectors for moved/renamed assets and documented dangling redirector caveats.

## Non-Negotiable Requirements

1. Scene and asset files must reference other assets by stable `AssetId`, not by source path, runtime `ModelHandle`, `ModelGPUHandle`, RHI handle, or generated artifact path.
2. `AssetId` must not be path-derived. `AssetId::from_path()` is compatibility-only and must not be used by serialized scene data or new asset authoring.
3. Asset identity, source location, content hash, importer type, importer version, and import settings are distinct concepts.
4. Asset moves and renames must update registry path metadata without changing `AssetId`.
5. Asset deletion must be mediated by the asset database. Raw file deletion is detected as "missing source" or "missing metadata", not silently treated as a new asset.
6. Assets can reference assets. The registry must record dependencies and reverse dependencies for models, materials, textures, scenes, scripts, prefabs, shaders, fonts, and future custom asset types.
7. Deleting an asset with strong inbound references must be blocked by default. Force-delete must leave explicit broken references that tools can report and repair.
8. Redirects/tombstones must be represented deliberately. A moved or replaced asset can leave `old AssetId -> new AssetId` or `old path -> AssetId` redirect metadata for migration, but redirect chains must be bounded, validated, and fixable.
9. CPU asset data and GPU residency must be separate. CPU loading/importing belongs to the engine asset/resource layer; GPU buffers/textures/instances belong to renderer-facing residency services.
10. `ModelGPUMgr` should not own source asset loading long term. It can remain the meshlet renderer's residency allocator/uploader if fed imported CPU model data by an asset service.
11. Asset services must be engine/runtime services, not globals. `ResourceManager::get()` should disappear from runtime paths.
12. Runtime caches use explicit acquire/release or leases keyed by `AssetId`. Serialized data never stores cache handles.
13. The registry must support editor and runtime modes. Editor mode can scan, import, reimport, write metadata, and repair references. Runtime mode should load a cooked/read-only manifest.
14. The first implementation must keep Vulkan and Metal viable. Asset service APIs cannot expose Vulkan-only objects; renderer residency should continue to work through RHI abstractions.
15. The system must be testable without a GPU for registry, dependency, delete, move, and CPU-load behavior.
16. Asset creation must be scriptable. A Python script or small command-line import tool should be able to register source files, create/update metadata, assign durable IDs, create scene assets for demo presets, and validate dependency references without starting `vktest` or initializing a GPU device.

## Target Architecture

```text
Project Content
  source files
  asset metadata sidecars
  generated imported artifacts
  cooked manifests later

AssetDatabase / AssetRegistry
  owns durable AssetId records
  maps AssetId <-> canonical project path
  records asset type, importer, settings, source hash, artifact hash
  records dependencies and reverse dependencies
  records redirects, tombstones, missing-source diagnostics

AssetService
  engine-owned runtime/editor service
  resolves AssetId to registry entry
  loads imported CPU asset data
  owns CPU asset cache and lifetime
  exposes typed asset access without renderer handles

RenderAssetResidency / renderer residency services
  resolves loaded CPU assets to GPU resources
  owns GPU resource lifetime
  exposes renderer-local handles to renderer implementations only

Scene / Flecs World
  stores AssetId references in components
  never stores source paths or runtime/GPU handles
```

Recommended code ownership:

- `src/engine/assets/`: `AssetId` policy, registry records, dependency graph, asset database, asset service, typed CPU asset interfaces.
- `src/engine/scene/`: scene components that reference `AssetId`.
- `src/gfx/`: imported GPU-neutral model/texture data structures that are reusable by renderers.
- `src/gfx/renderer/`: renderer residency, including a refactored `ModelGPUMgr` or replacement model residency service.
- `apps/vktest/`: compatibility authoring only. It may seed demo assets into the database during migration but should not define final asset rules.

## Asset Identity

`AssetId` should become a durable generated identifier, preferably a 128-bit UUID-style value with a stable text representation. The current `uint64_t` type was acceptable to create an early type boundary, but it is too easy to collide, too small for external tools, and currently encourages path-hash thinking.

Requirements:

- ID generated once when an asset is imported or explicitly registered.
- ID persists in metadata and version control.
- ID does not change when content, path, or import settings change.
- Content hash changes trigger reimport; they do not change identity.
- Duplicate content can exist under different asset IDs.
- Importing the same file after metadata loss is not silently accepted as the same asset. It is a repair operation.

Migration rule: keep `AssetId::from_path()` only under clearly named compatibility code until demo presets are registered as real assets. New serialized scenes and asset records must reject path-derived IDs.

## Registry Storage

The durable source of truth should be per-asset metadata plus a generated aggregate index:

- Sidecar metadata travels with source assets and keeps version-control conflicts local.
- Aggregate registry/index is generated or rebuilt for fast startup, validation, and runtime/cooked manifests.

Initial sidecar shape can be text (`.tasset`, `.asset.toml`, or similar) with:

- `id`
- `type`
- `source_path`
- `display_name`
- `importer`
- `importer_version`
- `import_settings`
- `source_content_hash`
- `imported_artifact_hash`
- `dependencies`
- `labels/tags`
- `schema_version`

Initial aggregate registry can live under `resources/assets.registry.*` or `resources/local/asset_cache/` depending on whether it is checked in. The durable IDs must live in checked-in metadata; generated import artifacts and caches can stay out of source control.

Open decision before implementation: exact file extension and serialization format. TOML/YAML are friendlier for human review; JSON is simpler if an existing dependency exists; binary is inappropriate for the first editor-facing registry.

## Asset Types

The registry must be type-extensible. Do not implement a closed `enum` that requires central edits for every future asset class.

Minimum initial asset kinds:

- `model`: source glTF/GLB today; imported CPU model data includes geometry, meshlets, materials, texture dependencies, hierarchy/default transforms.
- `texture`: PNG/KTX2/etc.; imported CPU texture or compressed GPU-ready payload.
- `material`: references texture assets and shader/material parameters.
- `scene`: serialized Flecs scene data; references models, textures, materials, prefabs, scripts.
- `shader`: shader source and include dependencies; initially separate from runtime asset loading if needed, but should not be impossible to register.
- `font`: current ImGui/font path can become a registered asset later.

Future-ready kinds:

- `prefab`
- `script`
- `audio`
- `sprite atlas`
- `voxel palette/world`
- `animation`
- custom game/editor asset types registered by modules

## Dependency Model

Dependencies must be explicit graph edges:

- Strong dependency: required to load correctly. Example: material -> albedo texture; model -> imported material asset; scene -> model used by entity.
- Soft dependency: optional/load-on-demand. Example: streaming variant, editor preview asset, fallback.
- Generated dependency: import artifact depends on source and importer settings.
- Tooling dependency: editor-only thumbnails, source documents, metadata.

Registry operations need:

- `dependencies(asset_id)`
- `dependents(asset_id)`
- transitive closure for packaging/cooking
- cycle detection where cycles are illegal
- allowed cycles where appropriate, such as prefabs/scenes only if the serialization policy can handle them
- stale dependency detection after reimport

Deletion policy:

- Delete with strong dependents: block and report all dependents.
- Delete with only soft dependents: allow with warning and convert soft refs to missing state.
- Force delete: mark asset tombstoned, remove or quarantine source/artifacts, preserve enough metadata for diagnostics and undo/repair.
- Restore: same `AssetId` if the tombstone/metadata still exists.
- Purge tombstone: allowed only when no references or redirects remain.

Move/rename policy:

- Update path metadata and sidecar location.
- Preserve `AssetId`.
- Record a path redirect if external data may still point at the old path during migration.
- Provide a fixup command that rewrites stale references and removes resolved redirects.

Replace/merge policy:

- Replacing file contents keeps `AssetId` and triggers reimport.
- Replacing one asset with another should create an `AssetId` redirect only through an explicit tool action.
- Redirect chains must collapse during fixup; cycles are invalid.

## Import Pipeline

The asset service should separate source import from runtime loading:

```text
source file + metadata + importer settings
  -> importer
  -> imported CPU artifact / cooked artifact
  -> runtime CPU asset cache
  -> renderer GPU residency
```

For models, split today's `ModelGPUMgr::load_model()`:

- `ModelImporter`: reads glTF/GLB/source file and produces CPU-side imported model data.
- `ModelAsset`: stable loaded CPU model asset containing hierarchy, meshes, meshlet data, material references, texture references, bounds, and any renderer-neutral CPU payload.
- `ModelResidency` or refactored `ModelGPUMgr`: uploads CPU model data into GPU geometry/material/texture allocations and returns renderer-local handles.
- `ModelInstanceResidency`: allocates per-entity/per-renderable GPU instance data from extracted transforms.

Important: "model asset" and "model instance" are separate. A model asset is shared. Scene entities instantiate it with transforms and per-instance overrides. Current `ResourceManager::load_model()` copies a `ModelInstance` and allocates a GPU instance in one operation; that coupling must be removed.

## Runtime Loading And Lifetimes

The runtime should expose typed asset access:

- `AssetService::get<ModelAsset>(AssetId)`
- `AssetService::load_async<ModelAsset>(AssetId)` later
- `AssetService::release(AssetLease)` or RAII lease handles
- status values: unloaded, loading, loaded, failed, missing, wrong type, stale import

The renderer should request residency through a renderer-facing service:

- Input: `AssetId` plus typed CPU asset reference.
- Output: renderer-local GPU handles used only inside renderer code.
- Lifetime: ref-counted or lease-based, released when no active renderables/caches need it.
- Failure: renderer gets a missing/fallback resource, and diagnostics retain the original `AssetId`.

`RenderScene` may continue carrying `AssetId` for now. A later extraction pass can optionally produce a resolved render packet, but the resolution must still happen outside Flecs scene data.

## ModelGPUMgr Ownership Direction

`ModelGPUMgr` is not inherently bad; its current placement and API are.

Long-term options:

1. Keep `ModelGPUMgr` under `gfx` as mesh/model GPU residency machinery, but remove source-file loading from it.
2. Move ownership from generic `RenderService` into the meshlet renderer if it is meshlet-specific.
3. Introduce a generic `RenderAssetResidency` service owned by `RenderService`, with model/texture/material residency subservices used by renderers.

Recommended path:

- First refactor `ModelGPUMgr::load_model(path, ...)` into CPU import plus GPU upload so it can accept `ModelAsset`/`ModelLoadResult` data.
- Keep `ModelGPUMgr` owned by `RenderService` during the bridge if that minimizes churn.
- Once 2D renderer work starts, move meshlet-specific model residency either into `gfx::MeshletRenderer` or behind a `RenderAssetResidency` interface so a 2D-only renderer is not forced to construct meshlet model buffers.

Exit condition for this phase: `RenderService` no longer exposes `model_gpu_mgr()` to app compatibility code for `ResourceManager::init()`.

## Migration Scaffolding

`ResourceManager`

- Current role: global path cache, CPU model cache, GPU model resource cache, per-instance handle pool.
- Required retirement: no engine/runtime path calls `ResourceManager::get()`.
- Compatibility allowance: a temporary adapter may translate asset-service model loads into existing `ModelGPUMgr` calls while the model import/upload split lands.

`DemoSceneEcsBridge`

- Current role: authors demo Flecs entities, computes path-hash `AssetId`s, registers `AssetId -> path`, loads model handles, syncs transforms.
- Required retirement: demo presets use registered asset IDs from the asset database; model loading is handled by engine asset service and renderer residency.

`AssetId::from_path()`

- Current role: deterministic demo bridge ID.
- Required retirement: only tests or migration tools may call it, and production serialized data must not contain path-hash IDs.

`RenderService::model_gpu_mgr()`

- Current role: exposes renderer residency machinery to app compatibility code.
- Required retirement: renderer internals are not reachable from scene/demo/app asset loading.

## Phased Implementation

### Phase 6.0: Plan And Guardrails

Deliverables:

- Land this plan.
- Add short comments or TODOs only where they prevent accidental new dependencies on `ResourceManager` or `AssetId::from_path()`.

Exit criteria:

- Agreement on asset identity, deletion policy, CPU/GPU split, and ModelGPUMgr direction.

### Phase 6.1: Asset ID And Registry Records

Deliverables:

- Replace or extend `AssetId` with a durable generated representation and text parse/format helpers.
- Add `src/engine/assets` with registry record types, asset status, asset type IDs, dependency edge types, redirects, and tombstones.
- Add GPU-free tests for ID parsing, record validation, duplicate ID detection, redirect cycle rejection, dependency graph queries, and missing asset status.

Exit criteria:

- New asset records can be created and validated without device/window/renderer initialization.
- Path-derived IDs are isolated to compatibility code.

### Phase 6.2: Registry Storage And Project Scan

Deliverables:

- Define sidecar metadata format and aggregate registry/index behavior.
- Implement load/save/scan for asset metadata under `resources/`.
- Detect missing source, missing metadata, duplicate IDs, moved metadata, and stale source hashes.
- Provide register/move/rename/delete/fixup operations in code, even if no editor UI exists yet.

Exit criteria:

- A test fixture can create assets, move them, delete them, and verify registry diagnostics and reference behavior.

### Phase 6.3: CPU Asset Service

Deliverables:

- Add engine-owned `AssetService`/`AssetDatabase` to `Engine` and `EngineContext`.
- Add typed CPU asset cache and loader interface.
- Split current model loading so CPU model import can run without `ModelGPUMgr`.
- Register demo model assets in metadata instead of `DemoSceneEcsBridge` static path maps.

Exit criteria:

- Scene/demo code can resolve `AssetId -> ModelAsset` without `ResourceManager`.
- CPU asset tests do not require GPU.

### Phase 6.4: Renderer Residency Bridge

Deliverables:

- Refactor `ModelGPUMgr` to accept CPU model asset data for upload/residency.
- Add a renderer residency bridge that maps `AssetId`/`ModelAsset` to `ModelGPUHandle`.
- Move per-renderable instance allocation out of `ResourceManager` and into renderer/extraction/residency flow.
- Keep meshlet demo behavior unchanged.

Exit criteria:

- `ResourceManager::init({ctx.renderer().model_gpu_mgr()})` is gone.
- `RenderService::model_gpu_mgr()` is no longer needed by app/demo code.
- `vktest --quit-after-frames 30` renders demo presets through AssetService + renderer residency.

### Phase 6.5: Delete ResourceManager

Deliverables:

- Remove global `ResourceManager` from runtime code.
- Convert remaining call sites to asset service or renderer residency.
- Remove path-hash model cache.

Exit criteria:

- No runtime/app path includes `ResourceManager.hpp` except possibly an old archived graphics experiment.
- Repeated scene load/unload returns CPU and GPU asset instance counters to baseline.

### Phase 6.6: Cooked Runtime Manifest

Deliverables:

- Generate a read-only runtime manifest from the editor registry.
- Include dependency closure for selected scenes.
- Map asset IDs to cooked artifact paths.
- Keep editor-only metadata out of runtime load paths.

Exit criteria:

- Runtime can load a scene's dependency closure from a manifest without scanning source directories.

## Validation Strategy

Use the standard repo command after implementation slices:

```bash
./scripts/agent_verify.sh
```

Add focused tests before broad renderer rewrites:

- Registry record parse/serialize roundtrip.
- Duplicate `AssetId` rejection.
- Stable ID after move/rename.
- Missing source detection.
- Delete blocked by strong dependent.
- Force delete leaves broken reference diagnostics.
- Redirect resolution, redirect chain collapse, redirect cycle rejection.
- Model CPU import without GPU device.
- Scene load/unload releases model instance residency.
- Demo preset registration resolves registered `AssetId`s, not path hashes.

Runtime smoke tests:

```bash
./build/Debug/bin/vktest --quit-after-frames 30
```

For HLSL or shader asset changes, keep using `teng-shaderc --all` through `agent_verify.sh`.

## Preset Asset Authoring

The current numbered `vktest` presets should become data assets, not hardcoded C++ branches. A scriptable asset workflow is the right migration path:

```text
Python/CLI preset importer
  -> registers model source assets if missing
  -> creates or updates scene asset metadata for presets 1-9
  -> writes Flecs scene data containing entities, transforms, cameras, lights, and AssetId references
  -> validates all model/material/texture references through the registry
  -> leaves vktest as a thin loader for selected scene asset IDs
```

Requirements:

- Presets 1-9 are represented as scene assets with stable `SceneId`/`AssetId` references.
- The script is idempotent: rerunning it updates existing preset assets instead of minting new IDs every time.
- The script can run in CI or from a developer shell without window/device/renderer startup.
- Demo-specific random presets must use stored generated data or a fixed seed recorded in metadata so generated entities keep stable IDs.
- Preset selection in `vktest` becomes "load scene asset N" rather than "run C++ code that creates entities and calls `ResourceManager`".
- Once this exists, `DemoSceneEcsBridge` should stop owning `AssetId -> path`, model loading, and transform sync. At most, `vktest` keeps a small compatibility UI listing preset scene assets.

This is the point where the hacky `vktest` preset/resource bridge can be deleted: the runtime loads scene assets through `SceneManager`, resolves asset references through `AssetService`, and renderer residency handles GPU resources behind the render boundary.

## Risks

- The `AssetId` type exists already. Leaving it as path-hash `uint64_t` will look cheaper but will lock scenes into unstable identity semantics.
- Splitting model CPU import from GPU upload may expose assumptions in `ModelInstance`, `ModelLoadResult`, material allocation, and texture upload ownership.
- Introducing a registry without dependency/delete policy would create broken-reference debt immediately.
- Moving `ModelGPUMgr` too early could churn renderer code before the CPU/GPU split is clear. First remove source loading from its API, then decide final ownership.
- Sidecar metadata can drift if files are moved outside tools. The scanner must detect this and produce repair diagnostics.
- Redirectors solve migrations but can become permanent clutter. Fixup tooling and purge criteria need to exist from the start.

## Open Questions

- Exact `AssetId` representation: 128-bit UUID type, two `uint64_t`s, or string-backed ID.
- Exact metadata format and extension.
- Should source sidecars live beside files or in a mirrored metadata tree under `resources/assets/` for generated/external sources?
- Which model import data should be stored as imported artifacts versus recomputed on startup during the first implementation?
- How should shader source/import metadata relate to the existing `teng-shaderc` dependency output?
- Should the first asset service be synchronous only, or should its API shape reserve async load states immediately?
- Where should fallback assets live for missing models/textures/materials?
- How much editor UI is needed before delete/move/fixup operations become usable?
