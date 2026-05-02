# Editor play mode vs edit mode (long-term intent)

**Parent roadmap:** [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) — Phase 9 (editor foundation).

**Status:** Intent for architecture and Phase 9 implementation; not all mechanics are specified below—extend this note when editor code lands.

---

## North star

**Play mode** runs the game loop (`Engine::tick()`, Flecs systems, input, future scripting) against a **live runtime copy** of the scene—not against the same Flecs world the editor treats as the authoritative **edited** document.

This matches the usual Unity / Unreal mental model: simulation mutates a **session world**; stopping play **does not** silently redefine what is on disk or what the editor considers the saved scene.

**Edit mode** keeps the **edit world** as the target of hierarchy, inspector, and **Save** (canonical `*.tscene.json` via the scene serialization registry). The edit world is what authors merge in version control.

---

## Rules (long-term)

1. **Two worlds while playing:** Editor holds an **edit** `Scene` (Flecs world) and, during play, a separate **play** `Scene` (or equivalent isolated runtime world) built from a **snapshot** of the edited scene at **Play** time (or from last explicit reload policy—see open points).

2. **Stop play:** Tear down the **play** world. The **edit** world remains as it was **before** play began, except where the product deliberately adds “apply play changes” workflows later (not the default).

3. **Save while editing:** Serialization reads the **edit** world only. Saving during play either writes the **edit** world (typical: disabled or warns—“cannot save scene while playing”) or is explicitly designed—pick one product rule when implementing; default stance here is **do not treat the play world as the save source**.

4. **Runtime-only state** (`LocalToWorld`, input snapshots, caches, ephemeral controllers) lives only in the **play** world unless a component is explicitly **authored** and registry-serialized per [`scene_serialization_design.md`](scene_serialization_design.md).

5. **Shared code:** Runtime exe and editor share `Scene`, `SceneManager`, serialization, extraction, and `RenderService`; they differ in **which world is active for editing vs ticking**, and in editor-only layers (see migration plan).

---

## Open points (decide when implementing Phase 9)

- **Enter play:** Snapshot strategy (deep copy of ECS state vs reload-from-staged JSON buffer)—must preserve `EntityGuid` stability and match serialization semantics.
- **Reload during play:** Whether “reload scene” affects only play, only edit, or both; error if ambiguous.
- **Apply changes:** Optional future command to merge selected entities/components from play → edit (explicit user action, not default on stop).

---

## References

- Undo/redo stacks **edit** mutations only; timing vs play mode: [`editor_undo_redo.md`](editor_undo_redo.md).
- Phase 9 scope and ordering: [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) § Phase 9, § Immediate priorities.
- Serializable vs runtime-only components: [`scene_serialization_design.md` — Authored vs runtime-only](scene_serialization_design.md#authored-vs-runtime-only).
- Single Flecs **process** / linkage: [`library_linkage_architecture_plan.md`](library_linkage_architecture_plan.md).
