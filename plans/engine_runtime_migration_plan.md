# Engine direction and phased delivery

**North-star:** runtime, editor, tools, and shipped games on one architecture—genre-capable (platformers, 2D, later voxel-scale verticals), not only a meshlet demo host.

**Snapshot (today):** `apps/metalrender` hosts `engine::Engine`; default renderer is `gfx::MeshletRenderer` via `RenderService`. `AssetId`, registry/DB, `AssetService`, and render-side model residency exist. Canonical scene files (`*.tscene.json`) load/save through registry-driven JSON serialization; GPU-free validation/cook/dump lives in `teng-scene-tool`. Demo data: `scripts/generate_demo_scene_assets.py`.

**Forward work** will **break** narrow on-disk formats, `RenderScene` shape, and internal CMake where the plan says so—see [Compatibility](#compatibility-and-intentional-breakage).

Do not reintroduce app-specific scene subclasses, deleted demo bridges, or monolithic app-owned render graphs.

## Principles

- `Engine::tick()` is first-class; `run()` is convenience. Editor/play-mode drives `tick()` manually.
- Scenes are **data-first Flecs worlds**, not C++ scene subclasses. Behavior = systems (+ optional services), scripts later.
- **Editor = separate build target**, same engine libraries, editor-only layers and authoring linkage (`metalrender` stays lean).
- **Presentation ≠ simulation:** `RenderScene` / `IRenderer` vs ECS; gameplay does not own `RenderGraph` or GPU handles as authored state.
- **Vulkan and Metal** stay viable through RHI/platform code; avoid Vulkan-only assumptions in engine-level logic.
- **Stable IDs** (`SceneId`, `EntityGuid`, `AssetId`) anchor authored data; Flecs entity ids and GPU handles stay runtime-only.
- **Library boundaries** intentional—runtime must not link editor asset mutation. Details: `plans/library_linkage_architecture_plan.md`.
- **Shipped runtime linkage (long-term, strict):** Player/game targets **statically link** ECS (scene/Flecs) and **core engine runtime** libraries—Godot/Unity/Unreal-style standalone binary, not core simulation living in a separate versioned engine DSO. `metalrender` now links the `teng_runtime` interface aggregate over static runtime component libraries. Full rule: `plans/library_linkage_architecture_plan.md` (“Long-term requirement”).

## Goals

- **Runtime:** Platform, time, input, scenes, resources, `RenderService`, layer stack.
- **Simulation:** ECS + optional modules (physics, animation, audio)—not logic stuffed into `MeshletRenderer`.
- **Presentation:** Meshlet 3D default today; room for 2D, voxel-style renderers behind `IRenderer` / evolving `RenderScene`.
- **Authoring:** Editor exe, round-trip serialization, cooked pipelines—replace hand-maintained loaders over time.
- **Quality gates:** `./scripts/agent_verify.sh` and `metalrender --quit-after-frames 30` (regression bars, not “done”).

## Non-goals (near term)

Lua implementation, polished editor UX v1, full material-graph product, RHI/meshlet rewrites for non-goals, replacing every debug scene at once.

## Temporary non-goals (not permanent endpoints)

- Interim **scene loader + `*.tscene.toml` forever** — Phase 12 replaces/supersedes `SceneAssetLoader` with **JSON** canonical scenes (`*.tscene.json`, `nlohmann/json`) + registry + optional binary cook per [`scene_serialization_design.md`](scene_serialization_design.md). *(Project config such as `project.toml` is unrelated; scenes on disk are JSON after Phase 12.)*
- Single `RenderScene` / single renderer forever — 2D/voxel milestones may extend or version extraction.
- Stable **internal** CMake target names — Phase 8+ may rename splits.

## Scope honesty: not designed yet

The phases below build **architecture** (linkage, editor foundation, serialization, 2D, scripting hooks, simulation modules). They do **not** imply the following are specified, shipped, or safe to assume—unless code or a dedicated plan says otherwise.

| Area | Reality | Notes |
|------|---------|--------|
| **Editor depth** | Phase 9 is **foundation** (hierarchy, inspector, play/stop, reload). | Undo/redo, selection/multi-edit, prefabs/variants/overrides, gizmos, and rich **asset import/browser** UX are **not** scoped in these plans—add focused notes when that work starts. |
| **Play vs edit worlds** | Required semantics before heavy editor UI. | Short `plans/` note (see Open follow-up plans)—duplicate worlds, mutation during play, what reload means. |
| **Scripting** | Phase 11 is **prep** (metadata + scheduling hooks). | No VM choice, ECS binding surface, debug workflow, or sandbox policy—**do not** treat “Lua later” as designed API. |
| **Scene save vs player save** | Phase 12 is **scene/content** round-trip + cook. | **Player progression** (save games, checkpoints) is a **separate** concern—no format or pipeline here. |
| **Shipping / distribution** | Plans cover **cooked runtime layout**, **AssetId** closure, and **static player linkage** (`library_linkage_architecture_plan.md`). | Store packaging (Steam/Epic/etc.), codesigning/notarization, crash telemetry, release-only logging, and **release CI** are **outside** current plan docs until explicitly added. |
| **Input** | Window/input feed exists; layers consume events. | **Action maps**, **rebinding**, local/multiplayer input split—not specified. |
| **Networking** | No architectural stance. | Single-player-first is implicit; multiplayer/replication is **not** assumed or forbidden—decide in a future note if needed. |
| **Audio** | Listed under Phase 13 as an optional module spine. | No parallel to `render_service_extraction_design.md` / `asset_registry_implementation_plan.md` yet. |
| **Diagnostics & perf** | Smoke tests and dev iteration are defined (`AGENTS.md`). | Shipping **log tiers**, **crash hooks**, frame budgets/profiling policy for release builds—not roadmap items here. |
| **Validation beyond smoke** | `agent_verify`, scene smoke, bounded `metalrender`. | Headless simulation tests, golden-frame/compare, soak automation—not specified. |

**For agents and humans:** Do not implement or advertise features from the table as approved design without a plan slice or maintainer direction. **Cooked bundles ≠ commercial publish pipeline.** Missing rows are **gaps in product coverage**, not contradictions of the runtime principles above.

## Compatibility and intentional breakage

| Area | Likely to break | Keep stable (spirit) |
|------|-----------------|----------------------|
| Scene on-disk | Interim layout / loader until Phase 12; then **JSON** canonical + cooked binary per [`scene_serialization_design.md`](scene_serialization_design.md) | Stable **IDs** as authored references |
| `RenderScene` / extraction | New channels, fields | No `RenderGraph`/GPU in gameplay components |
| Asset pipeline | DB manifest layout | Scenes use **AssetId**, not paths |
| Internal CMake | Target names, split topology, removed shared `teng` | One Flecs/runtime process; `agent_verify` workflows; **long-term** shipped player = **static** ECS + core (`library_linkage_architecture_plan.md`) |

Ship version bumps, migration notes, and generator updates—no silent drift.

## Engine pillars

1. **Simulation** — Flecs, systems, optional services (physics/audio/…).
2. **Presentation** — Extraction → `RenderScene` → `IRenderer` (+ audio output later).
3. **Authoring** — Editor, hierarchy/inspector, save/load, play/stop.
4. **Tools & cook** — GPU-free CLIs, validation, cooked runtime bundles.

## Multi-genre direction

- **Platformer:** Same ECS/runtime; add collision/physics/animation modules later.
- **2D:** Phase 10 — sprites/sorting/`IRenderer` path without requiring meshlet code.
- **Voxel:** Dedicated milestone—chunk storage, streaming, renderer strategy—not a meshlet tweak.

## Extension model (physics, animation, sound)

Components (no GPU in serialized gameplay state) → systems → narrow services as needed → serialization registration → editor metadata (ties to Phase 12). Do not grow `RenderService`/`MeshletRenderer` into a universal subsystem host.

## Current state (pointers)

| Area | Where |
|------|--------|
| App host | `apps/metalrender/main.cpp` — `EngineConfig`, layers, `Engine::run()`, scene load, `enqueue_active_scene()` |
| Engine API | `src/engine/Engine.hpp`, `Layer.hpp`, `LayerStack.hpp`, `EngineContext.hpp` |
| Tick/layers record | `plans/engine_tick_layer_design.md` |
| Scene / ECS | `src/engine/scene/Scene.hpp`, `SceneComponents.hpp`, `SceneManager.hpp` |
| Render boundary | `src/engine/render/RenderService.*`, `RenderScene.hpp`, `IRenderer.hpp`; design note `plans/render_service_extraction_design.md` |
| Meshlet renderer | `src/gfx/renderer/MeshletRenderer.*` |
| Scene serialization | `src/engine/scene/SceneSerialization.*` |
| Build | `apps/CMakeLists.txt` (`metalrender`, `teng-shaderc`, `engine_scene_smoke`, `teng-scene-tool`); `src/CMakeLists.txt` defines static component libs, `teng_scene_tool_lib`, and the `teng_runtime` interface aggregate |

## Destination architecture

```text
Runtime exe     → EngineConfig, runtime/debug layers, scene/cooked load, run() or tick()
Editor exe      → + editor layers/tooling, manual tick for edit/play
Tools/scripts   → asset/scene cook & validate; no window/device unless needed
Engine          → platform, time, input, resources, scenes, RenderService, LayerStack, tick()
SceneManager    → load/unload, active world, reload / play-mode (editor)
Scene           → Flecs world, stable entity IDs, core + registered components
RenderService   → extract RenderScene, invoke IRenderer, present
IRenderer       → Meshlet (today), 2D, voxel/custom later
```

Scene = loaded data + registered systems, not a gameplay base class. Editor and game share runtime/scene/asset/render **code**; differences = CMake + layers + linked libs.

## Build targets and library boundaries

| Product | Role |
|---------|------|
| `metalrender` | Shipped-style runtime for data scenes; links `teng_runtime`, which resolves to static runtime component libraries per linkage plan |
| `metalrender_editor` (planned) | + authoring UI/libs |
| `teng_scene_tool_lib` / `teng-scene-tool` | GPU-free scene validate/migrate/cook library + CLI scaffold |
| `teng-shaderc`, future tools | Minimal link; no full renderer when unnecessary |

Target dependency direction (names may change):

```text
editor → teng_editor → assets_tools → assets_runtime → core
                  └→ scene / render / gfx / platform

game   → scene / render / assets_runtime / gfx / platform / core

tools  → core, assets_tools, scene serialization as needed
```

**Rules:** Runtime does not link editor-only mutation/UI. Runtime and editor share serialization + asset ID model + extraction. GPU-free tools do not pull `RenderService`/window/device. One Flecs runtime per process—`library_linkage_architecture_plan.md`. **Long-term:** shipped/player exe **strictly** matches static ECS+core linkage described there (editor/tools may differ).

## Render and asset flow

```text
Flecs scene → extract → RenderScene → IRenderer → RenderGraph / RHI
```

- Engine-owned frame boundary: `src/engine/render`. Low-level GPU/RG/shaders: `src/gfx`. No `SceneManager`/Flecs inside `gfx`.
- Runtime: `AssetService` resolves `AssetId` → CPU assets; `RenderService`/`ModelGPUMgr` own GPU residency. Serialized scenes hold **AssetId** only (no paths/GPU handles). Cooked manifests: `plans/asset_registry_implementation_plan.md`.

## Interim authoring (demos)

`scripts/generate_demo_scene_assets.py` emits **canonical JSON** `*.tscene.json`; runtime and tools use the registry + **`nlohmann/json`** only for scene bytes — see [`scene_serialization_design.md`](scene_serialization_design.md). Adding components: keep ECS, extraction, loader/generator, and docs aligned—or document gaps.

### Scene format v1 (interim, TOML — retired by Phase 12)

`schema_version = 1`, `name`, `[[entities]]`. Per entity: `guid`, `name`, `[transform]`; optional `local_to_world`, `camera`, `directional_light`, `mesh_renderable` (`model` = `AssetId` text—never `from_path()` in authored data). Missing `local_to_world` derives from transform. **Replaced by** JSON canonical layout (`registry_version`, `scene`, `entities[].components`, …) in Phase 12 — see [`scene_serialization_design.md`](scene_serialization_design.md).

### Canonical scenes (Phase 12 — normative)

[`plans/scene_serialization_design.md`](scene_serialization_design.md): **`*.tscene.json`**, **`nlohmann/json`**, component registry, cooked binary optional.

## Phased delivery

### Completed (0–8)

| Phase | Summary |
|-------|---------|
| 0–1 | Planning guardrails; `Engine`, `tick`/`run`, layers, window/device/swapchain in engine |
| 2 | Flecs submodule, `Scene`/`SceneManager`, stable IDs, core components, scene smoke |
| 3 | `RenderScene`, extraction, `RenderService`, `IRenderer`, `DebugClearRenderer` |
| 4–5 | Demo data in ECS; `MeshletRenderer` as default `IRenderer` |
| 6 | `AssetService`, registry/DB, render residency by `AssetId` (cooked split/manifests still open) |
| 7 | `metalrender` + `SceneAssetLoader` + generated interim scene files + `project.toml` startup paths |
| 8 | Static runtime component libs + `teng_runtime` aggregate; shared `teng` removed; GPU-free `teng_scene_tool_lib` / `teng-scene-tool` scaffold (`library_linkage_architecture_plan.md`) |

### Phase 9: Editor foundation

`metalrender_editor`, editor lib, `EditorLayer`, hierarchy, inspector, play/stop, reload; shared serialization/asset/render with runtime. **Exit:** edit data scene, play without restart, runtime exe without editor libs. **Before heavy UI:** play-mode semantics note in `plans/`.

### Phase 10: 2D path

Sprites/extraction as needed; `IRenderer` path for 2D data scene without meshlet dependency.

### Phase 11: Scripting prep

Metadata + scheduling hooks for future Lua (or other) without replacing scene/layer model.

### Phase 12: Serialization + cooked runtime

Round-trip save/load, schema versioning, component contract (reflection/tables/codegen), **canonical JSON** vs cooked binary, GPU-free validate/migrate where practical. **No backward-compat requirement** for interim scene v1 (`*.tscene.toml`)—replace or bulk-migrate wholesale to **`*.tscene.json`**. **Implementation spec (agents):** [`plans/scene_serialization_design.md`](scene_serialization_design.md) — registry, **`nlohmann/json`**, on-disk layouts, cook/validate CLI, CMake splits, sub-phases 12-A–E, exit criteria.

### Phase 13: Simulation modules spine

Physics/animation/audio as optional modules + systems + services; document fixed vs variable step if needed. **Exit:** omit unused modules at link time where CMake allows.

## Immediate priorities

1. Phase 12 slice — prove one component through shared serialize/deserialize before inspector / hand-maintained loader sprawl.
2. Phase 9 — editor exe + basics (after play-mode semantics stub).
3. Phase 10 — 2D proof.

**Smoke:** `./scripts/agent_verify.sh`; `metalrender --quit-after-frames 30`; `--scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30`. Shaders: `agent_verify` runs `teng-shaderc --all`.

## Open follow-up plans

| Topic | Status | Where |
|-------|--------|--------|
| Flecs / CMake pin | Done enough | `plans/flecs_scene_foundation_design.md` |
| Asset registry / cooked | Partial | `plans/asset_registry_implementation_plan.md` |
| Scene serialization v2 | Spec ready (Phase 12) | `plans/scene_serialization_design.md` |
| Editor play-mode | Before Phase 9 | Short `plans/` note |
| RenderScene evolution | Ongoing | `plans/render_service_extraction_design.md` |
| Metal parity checklist | Future | Before claiming Metal for new renderer work |
| Voxel vertical | Unscoped | Dedicated milestone when started |
| Linkage | Done (Phase 8) | `plans/library_linkage_architecture_plan.md` |
| Scope honesty / unstated product gaps | Living section | This doc, [§ Scope honesty](#scope-honesty-not-designed-yet) |
| Player save / progression | Not started | Distinct from Phase 12 scene serialization |
| Export & release pipeline | Not started | Distribution, signing, release diagnostics—see Scope honesty |
| Scripting contract (VM, bindings, safety) | Not started | After Phase 11 prep; separate plan |
| Input (actions, rebind) | Not started | Optional future `plans/` note |
