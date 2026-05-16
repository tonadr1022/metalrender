# Phase 10 editor foundation implementation plan

**Parent roadmap:** [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) — Phase 10.

**Status:** Ready-for-implementation plan. This document resolves the Phase 10 editor-foundation
decisions that were still open in [`editor_play_mode_semantics.md`](editor_play_mode_semantics.md).

## Goal

Ship the first real editor vertical:

- `metalrender` becomes a lean runtime/demo host again and does not link editor authoring UI.
- `teng_editor` is a separate executable with editor-only layers and scene-authoring linkage.
- The editor can open a schema-valid data scene, show an entity list and schema-driven inspector,
  mutate authored components through `SceneDocument`, save/reload the edit document, and enter/stop
  play mode without restarting the process.
- Play mode runs a runtime scene copy. Stop discards play-session mutations. Save always targets the
  edit document.

This phase is intentionally a foundation, not a full production editor.

## Repository state this plan is based on

- `apps/metalrender/main.cpp` constructs `engine::Engine`, loads a startup scene or `--scene`, pushes
  `DebugSceneAuthoringLayer`, pushes `ImGuiOverlayLayer`, and calls `run()`.
- `apps/metalrender/CMakeLists.txt` links `metalrender` against `teng_runtime` and
  `teng_scene_authoring`; this violates the roadmap direction that runtime does not link editor
  authoring UI.
- `src/engine/Engine.*` already provides `Engine::tick()`, `Layer`, `LayerStack`, input snapshots,
  `SceneManager`, `RenderService`, ImGui enable/disable, and startup scene loading.
- `src/engine/scene/authoring/SceneDocument.*` already provides dirty/revision tracking, entity
  create/rename/destroy, authored component add/remove/set/field edit, save/save-as, and mutation
  validation through canonical JSON candidates.
- `src/engine/scene/authoring/SceneAuthoringInspector.*` exposes editable component/field metadata
  from the frozen component registry.
- `SceneManager` can create, destroy, find, activate, and tick scenes, but it has no scene path
  metadata, no replacement/reload API, and no explicit edit/play role model.
- `deserialize_scene_json()` always creates a new scene with a generated `SceneId` and activates it.
- Scene serialization uses JSON v2 and stable `EntityGuid`s. Runtime-only derived state such as
  `LocalToWorld` is refreshed after load and after authored transform edits.
- The referenced roadmap docs `component_schema_authoring_model.md` and
  `scene_serialization_design.md` do not exist in this tree. The implementation in
  `src/engine/scene` and the smoke/unit tests are the current authoritative Phase 9 contract.

## Decisions locked for Phase 10

These are not open assumptions for implementers.

1. **Editor target name:** the executable is `teng_editor`; the reusable editor library is
   `lib_teng_editor`.
2. **Runtime target cleanup:** `metalrender` must stop compiling `DebugSceneAuthoringLayer` and must
   stop linking `teng_scene_authoring`.
3. **Editor UI backend:** use Dear ImGui and the existing renderer ImGui support; no new UI toolkit
   or docking framework is required for Phase 10. `EditorLayer` owns editor authoring UI, while
   `ImGuiOverlayLayer` remains a separate runtime/debug overlay path.
4. **Hierarchy scope:** Phase 10 ships a flat entity outliner. It may be called "Hierarchy" in the UI
   for future continuity, but this phase does not add parent/child transform semantics.
5. **Selection scope:** single-entity selection only.
6. **Inspector scope:** schema-driven editing for currently supported authored field kinds only.
   Unsupported or awkward field kinds should render read-only with a diagnostic label instead of
   inventing partial behavior.
7. **Enter play snapshot strategy:** serialize the edit scene to an in-memory JSON value, validate it,
   then deserialize it into a new runtime scene. This intentionally exercises the same schema path as
   save/load and preserves `EntityGuid`s.
8. **Play scene identity:** the play scene gets a fresh `SceneId` and a descriptive session name such
   as `<edit name> (Play)`. Authored `EntityGuid`s inside the play scene remain identical to the edit
   snapshot.
9. **Active scene policy:** edit mode activates the edit scene; play mode activates the play scene so
   `SceneManager::tick_active_scene()` and the default runtime render path continue to operate without
   scene-manager special cases. Editor camera work may add an explicit render submission API, but it
   should not change which scene is active in edit vs play mode.
10. **Save while playing:** disabled. Save writes only the edit document, and the editor must not save
    from the play scene.
11. **Editing while playing:** disabled for the edit document in the first vertical. The inspector may
    show the play scene as read-only/debug data later in Phase 10, but edit mutations do not apply
    while playing.
12. **Reload while playing:** disabled. The user must stop play before reloading the edit document.
13. **Stop play policy:** destroy the play scene, reactivate the edit scene, and preserve edit-scene
    dirty state exactly as it was before play.
14. **Undo/redo:** not in Phase 10 exit criteria. `SceneDocument` mutations must continue to pass
    through command/transaction-shaped APIs so the future undo stack in
    [`editor_undo_redo.md`](editor_undo_redo.md) can attach without replacing the editor UI.
15. **Asset browser/import UX:** not in Phase 10. Asset fields use raw `AssetId` string editing.
16. **Project format:** no project-file schema change in Phase 10 unless needed to load the startup
    scene path already present in `resources/project.toml`.
17. **Edit-mode scene ticking:** disabled. Edit mode renders the edit scene but does not progress
    Flecs simulation systems against the authored edit world.
18. **Edit-mode camera navigation:** editor viewport navigation uses editor-owned transient camera
    state, not an authored `Camera`, `Transform`, or `FpsCameraController` component. Moving through
    the scene in edit mode must not dirty the document and must not serialize to JSON.
19. **Game camera editing:** authored scene cameras are edited only through inspector/component
    commands. A later "align scene camera to view" command can copy editor viewport camera state into
    an authored camera explicitly, but that command is not part of Phase 10.
20. **Render camera ownership:** render submission takes a source-agnostic render camera/view value.
    The renderer must not care whether the camera came from an authored runtime entity, an editor
    viewport, a shadow pass, a reflection probe, a thumbnail preview, or a future split-screen view.

## Extensibility decision gates

These are the places where Phase 10 must either make a narrow decision before implementation or add
explicit migration scaffolding. Do not let these become incidental UI or renderer behavior.

1. **Editor mutation command boundary:** before implementing hierarchy or inspector mutations, decide
   and document the editor-side operation boundary that panels use to mutate the edit document. The
   first version may be small, but hierarchy/inspector UI should not directly scatter calls to
   `SceneDocument::*`. The boundary should record at least an operation label, affected stable
   `EntityGuid`s, the `SceneDocument` call to apply, the returned status/error, and a future
   undo/coalescing hook. Full undo/redo remains out of Phase 10, but the call path must be easy to
   wrap with undo later.
2. **Render submission API and frame lifetime:** before implementing editor viewport camera plumbing,
   decide the exact render handoff API and frame lifetime for a non-authored camera. Prefer an explicit
   per-submission value such as `RenderCamera`/`SceneView` passed to `RenderService` over a sticky
   global override. If a temporary frame override is used, it must be set and cleared within the same
   frame and covered by a test or smoke assertion.
3. **Render camera type naming:** do not introduce an engine-level type named bare `RenderView`
   without reconciling it with the existing `gfx::RenderView` renderer-internal type. Prefer a name
   that signals camera/submission semantics, such as `RenderCamera`, `SceneView`, or
   `RenderSubmissionView`.
4. **Scene swap atomicity:** reload and play-copy flows must be two-phase. Load or deserialize the
   replacement scene first, then switch active/edit/play state only after success. On failure, leave
   the current edit scene, active scene, document binding, selection, dirty state, and mode unchanged.
   Any partially-created scene from a failed operation must be destroyed before returning the error.
5. **Scene role and execution policy:** edit-mode pause must be modeled with per-scene metadata, not
   a global engine ticking boolean. Add a small scene role plus execution policy model now so edit
   documents, play-session copies, runtime scenes, and future previews can coexist without later
   replacing editor pause scaffolding.
6. **Panel ownership boundary:** `EditorLayer` orchestrates editor UI, but panel implementation should
   live behind small panel/helper units as soon as hierarchy and inspector become non-trivial. Panels
   receive an editor context/session/controller reference and do not own engine services.
7. **Inspector draft commit semantics:** before field widgets mutate components, decide the commit
   rule per widget kind. Each completed user gesture should produce one editor operation where
   practical, with a stable coalescing key reserved for future undo behavior. Draft widget state must
   be cleared on selection changes, component removal, reload, and play/edit transitions.

## Architecture

```text
apps/metalrender
  main.cpp
  links: teng_runtime
  pushes: ImGuiOverlayLayer only when runtime debug UI is explicitly enabled

apps/teng_editor
  main.cpp
  links: teng_runtime, lib_teng_editor
  constructs Engine, loads project/scene, pushes EditorLayer
  drives Engine::tick() through Engine::run() for the first vertical

src/editor
  EditorLayer
  EditorSession
  EditorDocumentController
  EditorSelection
  EditorPanels

src/engine
  unchanged owner of platform/window/input/render/tick

src/engine/scene/authoring
  remains GPU-free authoring API used by editor and tests
```

### New library boundary

Add `src/editor/` as an editor-only static library:

- `lib_teng_editor` links `teng_engine_runtime` and `teng_scene_authoring`.
- `lib_teng_editor` links ImGui because it is editor UI.
- `teng_runtime` and `metalrender` do not link `lib_teng_editor` or `teng_scene_authoring`.
- `teng_scene_authoring` remains GPU-free and reusable by tools/tests.

This preserves the Phase 8 linkage rule: runtime/player code links static runtime component
libraries, while editor-only document mutation and UI stay outside the runtime executable.

`EditorLayer` owns the editor authoring UI: menus, toolbar, panels, and editor-specific ImGui
commands. `ImGuiOverlayLayer` remains a separate runtime/debug overlay path for debug views,
renderer menus, and future cheat/debug UI in runtime builds. The two may both use Dear ImGui, but
editor authoring UI should not be routed through `ImGuiOverlayLayer`.

### Editor ownership model

`EditorSession` owns editor state, not engine services:

- Current mode: `Edit` or `Play`.
- Edit `SceneId`.
- Optional play `SceneId`.
- Open document path.
- `SceneDocument` for the edit scene.
- Editor viewport camera state for edit-mode navigation.
- Current selected `EntityGuid`.
- Last operation status message for non-modal errors.

`Engine` continues to own `SceneManager`, `AssetService`, `RenderService`, input, and the active scene.
The editor session points at engine-owned scenes and uses `SceneDocument` for edit mutations.

### Render camera/view model

The render path should distinguish extracted scene content from the camera/view used to render it.
Authored scene cameras remain extractable scene data, but render submission should take a
source-agnostic camera value so future multi-view features do not need editor-specific renderer
branches.

A suitable Phase 10 shape is:

```cpp
struct RenderCamera {
  glm::mat4 view{1.f};
  glm::mat4 projection{1.f};
  glm::vec3 position{};
  float near_plane{0.1f};
  float far_plane{10000.f};
};

enum class RenderViewKind {
  Runtime,
  Editor,
  Shadow,
  ReflectionProbe,
};

struct SceneView {
  RenderCamera camera;
  RenderViewKind kind{RenderViewKind::Runtime};
};
```

Naming and exact fields may be refined during implementation to match existing render structs. The
important invariant is that this camera/view value is not ECS-owned and is not serialized. Runtime
mode derives a `Runtime` view from the selected authored camera entity. Edit mode derives an `Editor`
view from editor-owned viewport camera state. The renderer can use `RenderViewKind` for view-specific
features such as editor selection overlays, but core scene rendering consumes only camera data.

Avoid naming the engine-level type just `RenderView` unless the existing renderer-internal
`gfx::RenderView` has been renamed or clearly separated; otherwise the term becomes ambiguous between
"camera used to render a scene" and "GPU resources for a meshlet view."

This model intentionally leaves room for split-screen, multiple editor viewports, minimaps,
reflection captures, shadow maps, render-to-texture previews, thumbnail generation, VR stereo eyes,
and portals.

### Edit-mode viewport camera

The editor viewport camera is not an ECS entity. It lives in `EditorSession` as transient UI state:

- position
- yaw/pitch or orientation
- movement speed
- projection settings needed by the renderer

In edit mode, the renderer should view the active edit scene through this editor camera. Scene cameras
remain visible and editable as scene objects, but they do not control the editor viewport unless a
future explicit "pilot camera" feature is designed.

The render path should support this by passing an editor `SceneView`/`RenderCamera` alongside the
scene render submission, not by adding editor-only components to the authored scene. The editor
layer/session owns the edit viewport camera and updates the render view once per frame. The render
view must be frame-scoped or explicitly cleared at end frame so an editor view cannot leak into a
later runtime render.

### Document controller

Add an editor-side controller around `SceneDocument`; do not put editor policy into
`SceneDocument` itself.

Responsibilities:

- Create/open the initial edit document from the scene that the editor loaded on startup.
- Keep `SceneDocumentOptions::path` set when a path is known.
- Expose dirty/revision/path information to panels.
- Save and reload according to Phase 10 policy.
- Enter play by serializing the edit scene to memory and deserializing a play scene.
- Stop play by destroying the play scene and reactivating the edit scene.
- Clear selection if the selected entity no longer exists after reload.

`SceneDocument` stays a narrow authoring transaction wrapper.

Editor panels should call through this controller/session command boundary for mutations rather than
holding their own policy around `SceneDocument`. The controller is the place where Phase 10 records
status now and where a future undo stack can wrap or replace operation application without rewriting
each panel.

## Public API changes to plan for

### Scene serialization

Add an API that returns the created scene instead of requiring callers to inspect global active state
or relying on deserialization side effects:

```cpp
[[nodiscard]] Result<SceneLoadResult> deserialize_scene_json_to_scene(
    SceneManager& scenes,
    const SceneSerializationContext& serialization,
    const nlohmann::json& scene_json);
```

Keep `deserialize_scene_json()` as a compatibility wrapper around the new API. The important Phase
10 need is deterministic "load JSON into a new scene and tell me which one". New load-from-JSON APIs
should not activate the scene; callers activate explicitly. If keeping a wrapper for existing callers,
the wrapper may preserve the old activate-after-load behavior until those callers migrate.

`SceneLoadResult` should keep the created `Scene*`; the redundant `SceneId` field can be removed
because `Scene` already owns its id. If a later refactor needs richer load metadata, add it then.

### Scene manager

Add minimal APIs needed by the editor:

```cpp
[[nodiscard]] const FlecsComponentContext& component_context() const;
bool destroy_scene(SceneId id); // already exists
bool set_active_scene(SceneId id); // already exists
```

Do not add parent/child hierarchy APIs in this phase.

### Scene document

Do not add Phase 10 API surface to `SceneDocument`. Bind the loaded path through
`SceneDocumentOptions::path` when the editor constructs the document.

### Engine/app startup

The editor needs to know which path was loaded. Prefer resolving this in `apps/teng_editor`
with the same option parsing as `metalrender`:

- `--scene <path>` opens that scene as the edit document.
- no `--scene` opens `resources/project.toml` `startup_scene`; the editor app parses the project
  file, resolves the concrete startup scene path, and passes that path to `Engine::load_scene()`.
- `--quit-after-frames <n>` remains available for smoke automation.

No broad `EngineConfig` project system is needed for Phase 10.

### Scene role and execution policy

Add per-scene runtime metadata so the editor can render the edit scene without progressing scene
systems, while runtime and play-session scenes keep default gameplay behavior:

```cpp
enum class SceneRole {
  Runtime,
  EditDocument,
  PlaySession,
  Preview,
};

struct SceneExecutionPolicy {
  bool receives_active_input{true};
  bool advances_simulation{true};
};
```

`SceneRole` records why a scene exists; `SceneExecutionPolicy` controls what `Engine::tick()` does with
the active scene this frame. Role must not become scattered behavior checks in engine code. Use it for
editor/session bookkeeping, diagnostics, scene-list filtering, and lifecycle assertions. Examples:

- stop play may destroy scenes with `SceneRole::PlaySession`, but must never destroy the
  `SceneRole::EditDocument` scene
- editor stats and scene lists can label or filter `EditDocument`, `PlaySession`, and future `Preview`
  scenes without guessing from names or active-scene state

Default scene creation should use `SceneRole::Runtime` and a default policy that receives active input
and advances simulation. `Engine::tick()` should still poll input, update time, draw ImGui,
extract/render the active scene, and run layers when the active scene policy disables simulation. It
should skip only the active scene input snapshot and `SceneManager::tick_active_scene()` according to
the active scene's `SceneExecutionPolicy`. Transient input state is still cleared exactly once per
frame whether scene simulation is enabled or disabled.

Editor policy:

- edit scenes use `SceneRole::EditDocument` with
  `{.receives_active_input = false, .advances_simulation = false}`
- enter play creates or activates a runtime copy with `SceneRole::PlaySession` and the default
  simulation policy
- stop play destroys the play-session scene, reactivates the edit scene, and relies on the edit
  scene's policy to keep simulation paused

## UI panels

### Main editor layer

`EditorLayer` owns `EditorSession` and draws:

- top menu/toolbar
- hierarchy/outliner
- inspector
- status bar
- scene stats/debug panel

The UI should be functional and restrained. It does not need marketing polish, docking, custom
icons, gizmos, or a content browser.

`EditorLayer` should orchestrate the editor shell. As hierarchy and inspector grow, split panels into
small `src/editor/panels/*` units that receive session/controller references. Panels must not own
engine services or encode save/play/reload policy locally.

The scene stats/debug panel is required, but its exact fields should be clarified at implementation
time against the available engine data. Keep the first version small and self-contained; suitable
fields include mode, active scene id/name, entity count, dirty/revision state, and selected entity.

### Toolbar/menu commands

Required commands:

- Save
- Reload
- Play
- Stop
- Create entity
- Delete selected entity

Command availability:

- Save enabled only in edit mode and when document has a path.
- Reload enabled only in edit mode and when document has a path.
- Play enabled only in edit mode.
- Stop enabled only in play mode.
- Create/delete enabled only in edit mode.

### Hierarchy/outliner

Phase 10 outliner is a deterministic flat list:

- list all entities with `EntityGuidComponent`
- display `Name.value` when present, otherwise lower-hex `EntityGuid`
- sort by display name then `EntityGuid`
- single click selects an entity
- create entity adds an authored entity, ultimately through `SceneDocument::create_entity`
- delete selected entity ultimately calls `SceneDocument::destroy_entity`

No parent/child drag/drop, no prefab nesting, no multi-select.

Create/delete should route through the editor mutation command boundary chosen for this phase, with
that boundary applying the underlying `SceneDocument` call. This keeps hierarchy actions undo-ready
without implementing the undo stack yet.

### Inspector

Inspector reads schema metadata from `editable_component_inspector()` and the selected entity.

Required behavior:

- show selected entity name and GUID
- rename entity through `SceneDocument::rename_entity`
- list authored editable components present on the entity
- edit supported fields through `SceneDocument::edit_component_field`
- add authored editable components not already present
- remove removable authored components
- reject invalid edits without mutating, surfacing the `Result` error in the status area

Field widgets:

- `Bool`: checkbox
- `I32`, `U32`: integer input
- `F32`: float input
- `String`: text input
- `Vec2`, `Vec3`, `Vec4`: float inputs
- `Quat`: four float inputs in schema order
- `AssetId`: text input for the raw asset id string in Phase 10
- `Enum`: combo if enum metadata is present, otherwise read-only
- `Mat4`: read-only in Phase 10

Edits should commit on explicit widget completion where practical, not on every rendered frame.
For ImGui this means using local draft values and committing simple values on
deactivation-after-edit. Complex values that cannot be safely committed that way get an explicit
Apply button.

Each committed inspector gesture should produce one editor mutation operation where practical. Numeric
drag/slider-like edits should reserve a stable coalescing key so a later undo stack can merge the
gesture into one undo step.

Draft widget state should be keyed by selected `EntityGuid`, component key, and field key, then
cleared on selection changes, component removal, reload, and play/edit transitions so stale drafts do
not apply to the wrong component.

## Play mode flow

### Enter play

1. Require edit mode.
2. Serialize the edit scene with `serialize_scene_to_json()`.
3. Validate/canonicalize through the existing scene serialization path.
4. Deserialize the JSON into a new scene.
5. Store the created play scene id from the returned `Scene*`.
6. Activate the play scene.
7. Switch mode to `Play`.
8. Disable edit commands and document mutation UI.

Do not save the scene to disk as part of play.

Enter play must be atomic from the editor user's point of view. If serialization, validation,
deserialization, or activation fails, destroy any partially-created play scene and leave the editor in
edit mode with the original active scene, document binding, selection, and dirty state unchanged.

### While playing

The existing `Engine::tick()` path ticks the active play scene and renders it. This is acceptable for
Phase 10. Editor UI still renders because `EditorLayer` is in the layer stack, but its authoring
controls are disabled.

If it stays simple, selection may continue to point at the same `EntityGuid` while playing and the
inspector may show read-only play-scene component data. This is useful later for copying play-mode
values back into the edit document, but Phase 10 must not introduce edit-document mutation while
playing to support it.

Potential issue to watch: `LayerStack::update()` currently runs after `SceneManager::tick_active_scene()`.
That means a future editor might want pre-simulation controls, but Phase 10 can leave the order alone.

### Stop play

1. Require play mode.
2. Clear selection if it was pointing at play-session-only state.
3. Destroy the play scene.
4. Reactivate the edit scene.
5. Clear play `SceneId`.
6. Switch mode to `Edit`.

The edit document's `dirty()` result must be unchanged by enter/stop play.

If reactivating the edit scene fails, report the failure and keep enough session state to avoid
saving from the play scene. This should be treated as an invariant failure in normal operation rather
than a recoverable product flow.

## Edit-mode camera flow

During edit mode:

1. `Engine::tick()` polls window/input and runs layers, but scene ticking is disabled.
2. `EditorLayer` updates `EditorViewportCamera` from input when the viewport has focus.
3. `EditorLayer` builds an `Editor` `SceneView`/`RenderCamera` from the editor viewport camera.
4. `RenderService` renders the active edit scene from that render view.
5. `SceneDocument::dirty()` is unchanged by viewport navigation.

The authored scene camera entity can still be selected and edited in the inspector. That is a normal
document mutation and should dirty the document.

## Save/reload policy

### Save

- Save is edit-mode only.
- Save calls `SceneDocument::save()`.
- Save failure is displayed in the status area.
- Successful save updates `saved_revision`.

### Reload

- Reload is edit-mode only.
- Reload requires a document path.
- If the document is dirty, Phase 10 should show a simple confirmation modal:
  "Reload and discard unsaved edits?"
- On confirm:
  - load the JSON from disk into a new scene
  - build the replacement scene successfully before changing the existing edit scene/document
  - release/rebuild any `SceneDocument` that references the old edit scene before destroying it
  - set the new scene active
  - rebuild `SceneDocument` with the same path
  - destroy the old edit scene after the replacement document is bound
  - clear selection if the selected entity no longer exists

Do not implement filesystem watching or automatic reload prompts in Phase 10.

Reload must be failure-atomic. If reading, validation, deserialization, activation, or document
rebind fails, destroy any newly-created scene and keep the old edit scene, active scene, document
binding, selection, dirty state, and mode unchanged.

## Implementation slices

### Slice 10.1 — Split runtime and editor targets (done)

Files and targets:

- Extend `src/CMakeLists.txt` with `lib_teng_editor`.
- Add `apps/teng_editor/CMakeLists.txt`.
- Add `apps/teng_editor/main.cpp`.
- Update `apps/CMakeLists.txt`.
- Update `apps/metalrender/CMakeLists.txt`.
- Stop compiling/linking `DebugSceneAuthoringLayer.*` into `metalrender`.

Exit:

- `metalrender` links `teng_runtime` only.
- `teng_editor` builds and launches the same startup scene with ImGui enabled.
- `teng-scene-tool` remains GPU-free.

`DebugSceneAuthoringLayer.*` can remain temporarily unlinked if `metalrender` is cleaned up before
the new editor layer has equivalent minimum functionality. Delete it in the first slice where it is
fully replaced.

### Slice 10.2 — Editor session and document controller

Files:

- `src/editor/EditorSession.hpp/.cpp`
- `src/editor/EditorDocumentController.hpp/.cpp`
- focused tests under `tests/smoke/EditorSessionTests.cpp`

Classes:

- `EditorSession`
- `EditorDocumentController`
- `EditorMode`
- `EditorSelection`

Responsibilities:

- bind to engine context on layer attach
- create `SceneDocument` for the edit scene
- preserve document path
- expose command availability
- centralize status/error messages
- define the editor mutation operation boundary that hierarchy/inspector panels will use later
- keep the operation boundary thin enough that the future undo stack can wrap it without panel rewrites

Exit:

- The controller can save and report dirty state for the loaded edit scene without any panel code.
- The plan or code documents the first editor operation shape before hierarchy/inspector mutations
  are implemented.

Validation:

```bash
./scripts/agent_verify.sh 
```

### Slice 10.3 — Serialization support for scene-copy play mode (done)

Files:

- `src/engine/scene/SceneSerialization.hpp/.cpp`
- `tests/smoke/SceneSerializationSmokeTest.cpp`

Work:

- add a load-from-JSON API that returns `SceneLoadResult` with the created `Scene*`
- remove the redundant `SceneLoadResult::scene_id` field unless implementation discovers a concrete
  need for separate metadata
- ensure deserializing an in-memory edit snapshot produces a distinct `SceneId`
- assert authored `EntityGuid`s are preserved
- ensure active scene behavior is explicit and new deserialization APIs do not activate scenes

Exit:

- Tests prove edit scene and play scene can coexist with distinct `SceneId`s and matching authored
  entity GUIDs.

### Slice 10.4a — Edit-mode simulation pause

Files:

- `src/engine/Engine.hpp/.cpp`
- `src/engine/scene/SceneManager.hpp/.cpp`
- `src/editor/EditorLayer.hpp/.cpp`
- focused tests for active-scene execution policy behavior

Work:

- add per-scene `SceneRole` metadata and `SceneExecutionPolicy`
- default runtime behavior remains `SceneRole::Runtime` with input snapshot and simulation enabled
- editor marks the edit scene as `SceneRole::EditDocument` with input snapshot and simulation disabled
- skip active scene input snapshot and active scene tick according to the active scene execution policy
- keep layer update/render/imgui and transient input clearing running when simulation is disabled
- keep role descriptive and policy behavioral; do not scatter `SceneRole::EditDocument` engine branches

Exit:

- The edit scene still renders while scene systems are paused.
- Runtime `metalrender` behavior is unchanged.

Validation:

```bash
./scripts/agent_verify.sh
./build/Debug/bin/metalrender --scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30
./build/Debug/bin/teng_editor --scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30
```

### Slice 10.4b — Render view/camera plumbing

Files:

- `src/engine/render/RenderScene.hpp`
- `src/engine/render/RenderSceneExtractor.hpp/.cpp`
- `src/engine/render/RenderService.hpp/.cpp`
- renderer implementation files as needed to consume the explicit render view
- focused tests for render view selection where practical

Work:

- decide the explicit render submission API and frame lifetime for non-authored cameras before
  editing renderer code
- add source-agnostic `RenderCamera`/`SceneView` or equivalent types
- avoid a bare engine-level `RenderView` name unless the existing `gfx::RenderView` ambiguity is
  resolved first
- derive a runtime render view from the primary authored scene camera
- allow callers to submit an explicit render view for a scene render
- ensure `RenderViewKind::Editor` or equivalent is available for editor-only overlay decisions
  without changing core scene extraction semantics
- ensure the editor view cannot leak into later runtime rendering

Exit:

- Rendering a scene no longer requires the render camera to be an authored ECS camera.
- Runtime rendering still uses authored camera data by default.
- Editor and runtime camera paths share the same render submission shape.

Validation:

```bash
./scripts/agent_verify.sh
./build/Debug/bin/metalrender --scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30
```

### Slice 10.4c — Editor viewport camera

Files:

- `src/editor/EditorViewportCamera.hpp/.cpp`
- `src/editor/EditorLayer.hpp/.cpp`
- focused tests for camera state and dirty-state behavior where practical

Work:

- add editor-owned viewport camera state
- update camera from editor input only when the viewport has focus
- submit an `Editor` `SceneView`/`RenderCamera` for edit-mode rendering
- prove edit viewport camera motion does not mutate authored `Transform` or dirty the document

Exit:

- Moving the editor camera through the scene changes only editor session state.
- The edit scene renders from the editor viewport camera in edit mode.

Validation:

```bash
./scripts/agent_verify.sh
./build/Debug/bin/teng_editor --scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30
```

### Slice 10.5 — Flat hierarchy/outliner panel

Files:

- `src/editor/EditorLayer.hpp/.cpp`
- `src/editor/panels/HierarchyPanel.hpp/.cpp`

Work:

- draw deterministic entity list
- single selection
- create/delete through the editor mutation operation boundary, which applies `SceneDocument`
  commands internally
- disable mutations in play mode

Exit:

- User can create, select, rename through inspector later, and delete entities in edit mode.

Validation:

```bash
./scripts/agent_verify.sh
./build/Debug/bin/teng_editor --scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30
```

### Slice 10.6 — Schema-driven inspector

Files:

- `src/editor/panels/InspectorPanel.hpp/.cpp`
- possible small helpers in `src/editor/EditorFieldWidgets.*`
- tests for field conversion helpers if they are non-trivial

Work:

- inspect selected entity
- render supported field widgets
- commit through the editor mutation operation boundary, which applies `SceneDocument` commands
  internally
- add/remove authored editable components
- show invalid edit errors without mutation
- make one completed user gesture produce one operation where practical, with coalescing keys
  reserved for future undo behavior

Exit:

- User can edit transform/camera/light/mesh-renderable data already represented in JSON v2.

Validation:

```bash
./scripts/agent_verify.sh
./build/Debug/bin/teng_editor --scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30
```

### Slice 10.7 — Save and reload UX

Files:

- `src/editor/EditorDocumentController.*`
- `src/editor/EditorLayer.*`
- `src/editor/panels/StatusBar.*` if split out

Work:

- Save command
- Reload command
- dirty-state indicator
- discard-unsaved-edits modal for reload
- status reporting
- failure-atomic reload that leaves the current edit scene/document untouched until the replacement
  scene and document binding are ready

Exit:

- Edit a scene, save it, reload it, and keep rendering without restarting.

Validation:

```bash
./scripts/agent_verify.sh
./build/Debug/bin/teng_editor --scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30
./build/Debug/bin/teng-scene-tool validate resources/scenes/demo_cube.tscene.json
```

For manual validation, use a copied temp scene rather than overwriting committed fixtures.

### Slice 10.8 — Play/stop runtime scene copy

Files:

- `src/editor/EditorDocumentController.*`
- `src/editor/EditorSession.*`
- tests for enter/stop play semantics

Work:

- serialize edit scene to memory
- deserialize play scene
- activate play scene
- disable edit/save/reload commands while playing
- stop and destroy play scene
- reactivate edit scene
- failure-atomic enter play that destroys partial play scenes and leaves edit mode untouched on error

Exit:

- Play/stop works repeatedly in one editor process.
- Stop does not mutate edit scene or dirty state.
- Runtime rendering uses the active play scene while playing.

Validation:

```bash
./scripts/agent_verify.sh 
./build/Debug/bin/teng_editor --scene resources/scenes/demo_cube.tscene.json --quit-after-frames 120
```

Manual validation should click Play and Stop repeatedly before the bounded smoke is considered enough.

### Slice 10.9 — Verification, docs, and roadmap cleanup

Files:

- `plans/engine_runtime_migration_plan.md`
- `plans/editor_play_mode_semantics.md`
- `AGENTS.md` only if command/target guidance changes
- `README.md` only if app run instructions should mention the editor

Work:

- mark Phase 10 plan as implemented or partially implemented as slices land
- update stale references to missing plan files
- add `teng_editor` to target lists and verification notes if default verification builds it
- update `agent_verify.sh` so the editor target builds by default

Exit:

- Future agents can discover and run the editor without reading commit history.

Validation:

```bash
./scripts/agent_verify.sh
```

## Tests to add

Required automated tests:

- editor mutation operation boundary records status/errors and affected stable ids for create/delete
  or a similarly small first operation
- editor document controller save/reload preserves path and dirty state
- dirty-document reload confirmation path discards edits only after confirmation
- enter play creates a distinct play scene and preserves authored `EntityGuid`s
- stop play destroys play scene, reactivates edit scene, and preserves edit dirty state
- repeated play/stop cycles in one process leave only the edit scene active and preserve edit dirty
  state
- edit commands are rejected or disabled while playing at the controller level
- edit-mode viewport camera movement does not mutate authored transforms or dirty the document
- disabling active scene simulation through `SceneExecutionPolicy` still allows render/layer updates
  to run
- disabling active scene simulation through `SceneExecutionPolicy` still clears transient input once
  per frame
- serialization in-memory load returns `SceneLoadResult` without relying on incidental active scene
  lookup or activating the loaded scene
- runtime/editor render-view selection is covered by small self-contained tests where practical

Additional tests:

- outliner sorting helper is deterministic
- inspector draft-to-JSON conversion for field widgets
- reload clears invalid selection

## Validation commands

Fast inner loop:

```bash
./scripts/agent_verify.sh 
```

Full exit check:

```bash
./scripts/agent_verify.sh
./build/Debug/bin/metalrender --quit-after-frames 30
./build/Debug/bin/metalrender --scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30
./build/Debug/bin/teng_editor --scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30
```

Use `./scripts/agent_verify.sh --full` before declaring the whole phase done if editor changes touch
renderer, shader, or app startup behavior.

## Risks and mitigations

- **Risk: editor UI leaks into runtime linkage.** Mitigation: make CMake checks/reviews verify
  `metalrender` links `teng_runtime` only and does not link `teng_scene_authoring` or `lib_teng_editor`.
- **Risk: play scene accidentally becomes the save source.** Mitigation: centralize save in
  `EditorDocumentController` and disable save while playing.
- **Risk: stale editor render view leaks into runtime rendering.** Mitigation: make render views
  explicit per submission or clear frame-scoped overrides at end frame.
- **Risk: in-memory JSON snapshot is slower than a future ECS clone.** Mitigation: acceptable for
  Phase 10 because it proves schema serialization semantics and keeps the first editor copy path
  deterministic.
- **Risk: inspector commits every frame.** Mitigation: field widgets must use draft values or
  deactivation/apply semantics.
- **Risk: flat "Hierarchy" creates false expectations.** Mitigation: document that parent/child
  transforms are outside Phase 10 and avoid implementing partial parenting in UI.
- **Risk: stale roadmap links confuse implementers.** Mitigation: Phase 10 cleanup updates missing
  plan references or creates replacement notes.

## Explicit non-goals

- Parent/child transform hierarchy.
- Gizmos or viewport picking.
- Multi-select and batch editing.
- Undo/redo stack.
- Prefabs, variants, overrides.
- Asset browser, import workflow, thumbnails, or drag/drop assignment.
- Filesystem watching and automatic external reload prompts.
- Script VM, script bindings, or script debugger.
- Editor-specific project format beyond opening the existing startup scene.
- Shipping/export/distribution pipeline.

## Phase 10 exit criteria

- `teng_editor` is a first-class target.
- `metalrender` does not link `teng_scene_authoring`, `lib_teng_editor`, or editor UI code.
- The editor opens a JSON v2 scene as an edit document.
- The editor can create/delete/select entities and edit supported authored component fields through
  `SceneDocument`.
- Save and reload work in edit mode with clear dirty-state behavior.
- Play creates a separate runtime scene copy without process restart.
- Stop destroys the play scene and returns to the unchanged edit document.
- Automated tests cover document controller, repeated play/stop semantics, dirty reload behavior, and
  the narrow render-view/tick-gating contracts.
- `./scripts/agent_verify.sh` passes.

No unresolved product-policy questions remain for Phase 10.
