# Engine direction and phased delivery

**North-star:** runtime, editor, tools, and shipped games on one architecture—genre-capable (platformers, 2D, later voxel-scale verticals), not only a meshlet demo host.

**Snapshot (today):** `apps/metalrender` hosts `engine::Engine`; default renderer is `gfx::MeshletRenderer` via `RenderService`. `AssetId`, registry/DB, `AssetService`, and render-side model residency exist. Canonical scene files (`*.tscene.json`) load/save through schema-driven scene serialization; GPU-free validation still lives in `teng-scene-tool.`

**Forward work** can **break** narrow on-disk formats, `RenderScene` shape, and internal CMake, extensiveley. this is pre alpha

## Principles

- `Engine::tick()` is first-class; `run()` is convenience. Editor **play mode** drives `tick()` manually against a **runtime copy** of the scene—see `[editor_play_mode_semantics.md](editor_play_mode_semantics.md)`.
- Scenes are **data-first Flecs worlds**. Behavior = systems (+ optional services), scripts later.
- **Editor = separate build target**, same engine libraries, editor-only layers and authoring linkage (`metalrender` stays lean).
- **Presentation ≠ simulation:** `RenderScene` / `IRenderer` vs ECS; gameplay does not own `RenderGraph` or GPU handles as authored state.
- **Cross platform Vulkan/Metal**
- **Stable IDs** (`SceneId`, `EntityGuid`, `AssetId`) anchor authored data; Flecs entity ids and GPU handles stay runtime-only.
- **Library boundaries** intentional—runtime must not link editor asset mutation. Details: `plans/library_linkage_architecture_plan.md`.
- **Shipped runtime linkage (long-term, strict):** Player/game targets **statically link** ECS (scene/Flecs) and **core engine runtime** libraries—Godot/Unity/Unreal-style standalone binary, not core simulation living in a separate versioned engine DSO. Full rule: `plans/library_linkage_architecture_plan.md` (“Long-term requirement”).

## Goals

- **Runtime:** Platform, time, input, scenes, resources, `RenderService`, layer stack.
- **Simulation:** ECS + optional modules (physics, animation, audio)—not logic stuffed into `MeshletRenderer`.
- **Presentation:** Meshlet 3D default today; room for 2D, voxel-style renderers behind `IRenderer` / evolving `RenderScene`.
- **Authoring:** Editor exe, round-trip serialization, cooked pipelines—replace hand-maintained loaders over time.
- **Quality gates:** `./scripts/agent_verify.sh` and `metalrender --quit-after-frames 30` (regression bars, not “done”).

## Temporary non-goals (not permanent endpoints)

- Single `RenderScene` / single renderer forever — 2D/voxel milestones may extend or version extraction.

## Scope honesty: not designed yet

The phases below build **architecture** (component schema/authoring model, editor foundation,
serialization/cook, 2D, scripting hooks, simulation modules). They do **not** imply the following are
specified, shipped, or safe to assume—unless code or a dedicated plan says otherwise.


| Area                                   | Reality                                                                                                                             | Notes                                                                                                                                                                                                                                                                                                                                                                  |
| -------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Component schema / authoring model** | Phase 9 is the next major refactor.                                                                                                 | `[component_schema_authoring_model.md](component_schema_authoring_model.md)` is authoritative: frozen registry, declarative C++ schemas, namespaced keys, schema-driven JSON/cook, diagnostics, and undo-ready authoring transactions.                                                                                                                                 |
| **Editor depth**                       | Editor foundation moves after Phase 9.                                                                                              | Full hierarchy, inspector UX, play/stop, reload prompts, save prompts, and undo stack implementation are **not** Phase 9 exits. **Undo/redo:** explicit **long-term** requirement—`[editor_undo_redo.md](editor_undo_redo.md)`. Selection/multi-edit, prefabs/variants/overrides, gizmos, rich **asset import/browser** UX remain **not** scoped until separate notes. |
| **Play vs edit worlds**                | Documented intent for the editor foundation phase after Phase 9.                                                                    | `[editor_play_mode_semantics.md](editor_play_mode_semantics.md)` — play = runtime copy; edit world authoritative for save; stop discards play world by default.                                                                                                                                                                                                        |
| **Scripting**                          | Later prep phase (metadata + scheduling hooks).                                                                                     | No VM choice, ECS binding surface, debug workflow, or sandbox policy—**do not** treat “Lua later” as designed API.                                                                                                                                                                                                                                                     |
| **Scene save vs player save**          | Scene/content serialization is schema-derived engine content.                                                                       | **Player progression** (save games, checkpoints) is a **separate** concern—no format or pipeline here.                                                                                                                                                                                                                                                                 |
| **Shipping / distribution**            | Plans cover **cooked runtime layout**, **AssetId** closure, and **static player linkage** (`library_linkage_architecture_plan.md`). | Store packaging (Steam/Epic/etc.), codesigning/notarization, crash telemetry, release-only logging, and **release CI** are **outside** current plan docs until explicitly added.                                                                                                                                                                                       |
| **Input**                              | Window/input feed exists; layers consume events.                                                                                    | **Action maps**, **rebinding**, local/multiplayer input split—not specified.                                                                                                                                                                                                                                                                                           |
| **Networking**                         | No architectural stance.                                                                                                            | Single-player-first is implicit; multiplayer/replication is **not** assumed or forbidden—decide in a future note if needed.                                                                                                                                                                                                                                            |
| **Audio**                              | Listed under the later simulation modules spine as optional module work.                                                            | No parallel to `render_service_extraction_design.md` / `asset_registry_implementation_plan.md` yet.                                                                                                                                                                                                                                                                    |
| **Diagnostics & perf**                 | Smoke tests and dev iteration are defined (`AGENTS.md`).                                                                            | Shipping **log tiers**, **crash hooks**, frame budgets/profiling policy for release builds—not roadmap items here.                                                                                                                                                                                                                                                     |
| **Validation beyond smoke**            | `agent_verify`, scene smoke, bounded `metalrender`.                                                                                 | Headless simulation tests, golden-frame/compare, soak automation—not specified.                                                                                                                                                                                                                                                                                        |


**For agents and humans:** Do not implement or advertise features from the table as approved design without a plan slice or maintainer direction. **Cooked bundles ≠ commercial publish pipeline.** Missing rows are **gaps in product coverage**, not contradictions of the runtime principles above.

## Compatibility and intentional breakage


| Area                       | Likely to break                                                                                                                                  | Keep stable (spirit)                                                                                                                               |
| -------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| Scene on-disk              | **JSON v2** canonical + cooked binary derived from component schemas — contract `[scene_serialization_design.md](scene_serialization_design.md)` | Stable **IDs** as authored references                                                                                                              |
| `RenderScene` / extraction | New channels, fields                                                                                                                             | No `RenderGraph`/GPU in gameplay components                                                                                                        |
| Asset pipeline             | DB manifest layout                                                                                                                               | Scenes use **AssetId**, not paths                                                                                                                  |
| Internal CMake             | Target names, split topology, removed shared `teng`                                                                                              | One Flecs/runtime process; `agent_verify` workflows; **long-term** shipped player = **static** ECS + core (`library_linkage_architecture_plan.md`) |


Ship version bumps, migration notes, and generator updates—no silent drift.

## Engine pillars

1. **Simulation** — Flecs, systems, optional services (physics/audio/…).
2. **Presentation** — Extraction → `RenderScene` → `IRenderer` (+ audio output later).
3. **Authoring** — Editor, hierarchy/inspector, save/load, play/stop.
4. **Tools & cook** — GPU-free CLIs, validation, cooked runtime bundles.

## Multi-genre direction

- **Platformer:** Same ECS/runtime; add collision/physics/animation modules later.
- **2D:** Phase 11 — sprites/sorting/`IRenderer` path without requiring meshlet code.
- **Voxel:** Dedicated milestone—chunk storage, streaming, renderer strategy—not a meshlet tweak.

## Extension model (physics, animation, sound)

Components (no GPU in serialized gameplay state) → declarative schema registration → systems →
narrow services as needed → schema-derived serialization/cook/editor metadata. Do not grow
`RenderService`/`MeshletRenderer` into a universal subsystem host.

## Destination architecture

```text
Runtime exe     → EngineConfig, runtime/debug layers, scene/cooked load, run() or tick()
Editor exe      → + editor layers/tooling, manual tick for edit/play
Tools/scripts   → asset/scene cook & validate; no window/device unless needed
Engine          → platform, time, input, resources, scenes, RenderService, LayerStack, tick()
SceneManager    → load/unload, active world, reload; constructed from one frozen component/schema context
SceneDocument   → authoring/edit document wrapper with transactions, dirty tracking, future undo hooks
Scene           → Flecs world, stable entity IDs, components registered from frozen schema context
RenderService   → extract RenderScene, invoke IRenderer, present
IRenderer       → Meshlet (today), 2D, voxel/custom later
```

Scene = loaded data + registered systems, not a gameplay base class. Editor and game share
runtime/scene/asset/render **code**; differences = CMake + layers + linked libs. Authoring mutation
goes through `SceneDocument`/scene authoring APIs, not direct editor writes into ECS.

## Library boundaries

**Rules:** Runtime does not link editor-only UI. Authoring/document mutation lives in a GPU-free  
authoring/tooling slice, separate from runtime scene ticking. Runtime and editor share component schema,  
serialization + asset ID model + extraction. GPU-free tools do not pull `RenderService`/window/device.  
One Flecs runtime per process—`library_linkage_architecture_plan.md`. **Long-term:** shipped/player exe  
**strictly** matches static ECS+core linkage described there (editor/tools may differ).

## Phased delivery

### Completed (0–8)


| Phase | Summary                                                                                                                                                                               |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0–1   | Planning guardrails; `Engine`, `tick`/`run`, layers, window/device/swapchain in engine                                                                                                |
| 2     | Flecs submodule, `Scene`/`SceneManager`, stable IDs, core components, scene smoke                                                                                                     |
| 3     | `RenderScene`, extraction, `RenderService`, `IRenderer`, `DebugClearRenderer`                                                                                                         |
| 4–5   | Demo data in ECS; `MeshletRenderer` as default `IRenderer`                                                                                                                            |
| 6     | `AssetService`, registry/DB, render residency by `AssetId` (cooked split/manifests still open)                                                                                        |
| 7     | `metalrender` + `SceneAssetLoader` + generated interim scene files + `project.toml` startup paths                                                                                     |
| 8     | Static runtime component libs + `teng_runtime` aggregate; shared `teng` removed; GPU-free `teng_scene_tool_lib` / `teng-scene-tool` scaffold (`library_linkage_architecture_plan.md`) |
| 9     | C++ code-gen schema registry, scene serialization                                                                                                                                     |


### Phase 10: Editor foundation

`metalrender_editor`, editor lib, `EditorLayer`, hierarchy/inspector basics, play/stop, reload/save
policy; shared serialization/asset/render with runtime and authoring APIs from Phase 9. **Exit:** edit
data scene, play without restart, runtime exe without editor UI libs. **Semantics:**
`[editor_play_mode_semantics.md](editor_play_mode_semantics.md)` (play = runtime scene copy; edit world
owns save). **Undo/redo:** required for serious authoring but may land as the next authoring-depth slice
after the first editor vertical—`[editor_undo_redo.md](editor_undo_redo.md)`.

### Phase 11: 2D path

Sprites/extraction as needed; `IRenderer` path for 2D data scene without meshlet dependency.

### Phase 12: Scripting prep

Metadata + scheduling hooks for future Lua (or other) without replacing scene/layer model.

### Phase 13: Cooked runtime / asset pipeline follow-through

Phase 9 includes enough cooked v2 work to remove the central cooked bit/codec model. Later cooked
runtime work focuses on asset bundle closure, runtime layout, cache invalidation, compression/streaming
choices, and distribution-adjacent tooling. **Contract:** `[scene_serialization_design.md](scene_serialization_design.md)`.

### Phase 14: Simulation modules spine

Physics/animation/audio as optional modules + systems + services; document fixed vs variable step if needed. **Exit:** omit unused modules at link time where CMake allows.

**Smoke:** `./scripts/agent_verify.sh`; `metalrender --quit-after-frames 30`; `--scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30`. Shaders: `agent_verify` runs `teng-shaderc --all`.

## Open follow-up plans


| Topic                                     | Status                                                       | Where                                                            |
| ----------------------------------------- | ------------------------------------------------------------ | ---------------------------------------------------------------- |
| Flecs / CMake pin                         | Done enough                                                  | `plans/flecs_scene_foundation_design.md`                         |
| Asset registry / cooked                   | Done                                                         | none                                                             |
| Component schema / authoring model        | Done                                                         | none                                                             |
| Scene serialization                       | Done                                                         | none                                                             |
| Editor play-mode                          | Intent documented for Phase 10 editor foundation             | `[editor_play_mode_semantics.md](editor_play_mode_semantics.md)` |
| Editor undo/redo                          | Long-term requirement (after authoring transaction boundary) | `[editor_undo_redo.md](editor_undo_redo.md)`                     |
| RenderScene evolution                     | Ongoing                                                      | `plans/render_service_extraction_design.md`                      |
| Metal parity checklist                    | Future                                                       | Before claiming Metal for new renderer work                      |
| Voxel vertical                            | Unscoped                                                     | Dedicated milestone when started                                 |
| Linkage                                   | Done (Phase 8)                                               | `plans/library_linkage_architecture_plan.md`                     |
| Scope honesty / unstated product gaps     | Living section                                               | This doc                                                         |
| Player save / progression                 | Not started                                                  | Distinct from scene/content serialization                        |
| Export & release pipeline                 | Not started                                                  | Distribution, signing, release diagnostics—see Scope honesty     |
| Scripting contract (VM, bindings, safety) | Not started                                                  | After Phase 12 prep; separate plan                               |
| Input (actions, rebind)                   | Not started                                                  | Optional future `plans/` note                                    |


