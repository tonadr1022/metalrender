# Editor undo / redo (long-term requirement)

**Parent roadmap:** [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) — Phase 9+.

**Status:** Requirement intent only; **not** part of the first Phase 9 merge slice (hierarchy, inspector, play/stop, save). Implement after the edit/play split is working—see [`editor_play_mode_semantics.md`](editor_play_mode_semantics.md).

---

## Requirement level

**Undo/redo is an explicit long-term requirement** for editor-grade authoring: authors must be able to reverse mistaken hierarchy edits, inspector changes, and similar **edit-world** mutations without relying on version control alone.

It is **not** a blocking exit criterion for the **initial** Phase 9 foundation milestone (prove editor exe + one vertical slice). Schedule it as the next authoring-depth milestone once scene editing is real.

---

## Scope (what stacks)

- **In scope (eventually):** Mutations to the **edit** `Scene` driven by editor UI—entity create/destroy/rename, component add/remove/field edits, parenting when hierarchy exists, multi-selection edits if supported.
- **Out of scope / separate:** Player-facing save-game rewind; GPU/resource editor state unless folded in deliberately.
- **Play mode:** Undo applies to the **edit** world only. Typical rule (recommended default): **do not** merge play-session simulation into the undo stack; either **freeze undo** while playing or clear/redact the stack on Play—pick one when wiring play mode. Play-world mutations are discarded on Stop per play-mode semantics.

---

## Architecture sketch (non-normative)

- **Command pattern** (or transactional batches): each user edit produces an **inverse** or a **before/after snapshot** sufficient to restore prior ECS state for affected entities.
- **Stable IDs:** Undo must preserve `EntityGuid` and authorship invariants expected by [`scene_serialization_design.md`](scene_serialization_design.md).
- **Coalescing:** Inspector drag / numeric fields often merge consecutive tweaks into one undo step (product polish).
- **Memory / bounds:** Cap stack depth or payload size for large edits; document tradeoffs when implementing.

---

## Open points

- Whether **global** undo spans only the active scene or includes project-level asset operations (later).
- Interaction with **prefabs/overrides** when those exist.
- Tests: deterministic replay of command sequences on a small scene fixture.

---

## References

- Phase 9 sequencing: [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) § Phase 9, § Scope honesty.
- Edit vs play worlds: [`editor_play_mode_semantics.md`](editor_play_mode_semantics.md).
