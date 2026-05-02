# Scene serialization — contract and roadmap

**Parent roadmap:** [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) (Phase 12 — largely implemented).  
**Related:** [`asset_registry_implementation_plan.md`](asset_registry_implementation_plan.md), [`library_linkage_architecture_plan.md`](library_linkage_architecture_plan.md), [`editor_play_mode_semantics.md`](editor_play_mode_semantics.md).

This file is the **authoritative contract** for canonical scene bytes and serialization semantics. Detailed algorithms live in `src/engine/scene/SceneSerialization.*` and tests; agents should read code there when changing behavior.

---

## Scope and non-goals

- **In scope:** Serialized **authored** scene content (entities, stable IDs, component payloads referenced by the registry), **canonical JSON** I/O, optional **cooked binary**, GPU-free **validate / migrate / cook** tooling.
- **Out of scope:** Player saves / progression (see migration plan [Scope honesty](engine_runtime_migration_plan.md#scope-honesty-not-designed-yet)). **Project** config files (e.g. `resources/project.toml`) may remain non-JSON; they only **reference** scene paths.
- **Backward compatibility:** Not a product guarantee pre-1.0. Bump `registry_version` and migrate content when breaking payloads or envelope rules.

---

## On-disk contract (canonical JSON)

- **Files:** UTF-8 JSON, convention **`*.tscene.json`**. Scene serialization uses **`nlohmann/json`** only for those bytes (no `toml++` on this path).
- **Envelope:** Top-level `registry_version` (int), `scene` (e.g. `name`), `entities` array. Entity records: `guid` (JSON number while `EntityGuid::value` stays within IEEE-754 safe integer range), optional top-level `name`, `components` map from **stable lower_snake string keys** to payload objects.
- **Ordering:** Entities sorted by **unsigned** `EntityGuid::value` ascending on save (same order cook/tools should assume when comparing).
- **Strictness:** Unknown top-level / envelope / component keys should fail validation and runtime load by default (tools may offer relaxed modes explicitly).
- **References:** Authored data uses **`EntityGuid`**, **`AssetId`**, **`SceneId`** semantics from `SceneIds.hpp`. No filesystem paths, Flecs runtime ids, or GPU handles in canonical scenes.

Illustrative shape (field names are indicative; exact payloads are defined by the registry in code):

```json
{
  "registry_version": 1,
  "scene": { "name": "demo" },
  "entities": [
    {
      "guid": 10001,
      "name": "camera",
      "components": {
        "transform": { "translation": [0, 0, 3], "rotation": [1, 0, 0, 0], "scale": [1, 1, 1] },
        "camera": { "fov_y": 1.047, "z_near": 0.1, "z_far": 10000.0, "primary": true }
      }
    }
  ]
}
```

---

## Authored vs runtime-only

**Serialized (examples today):** Types wired in the scene serialization registry — e.g. `Transform`, `Camera`, `DirectionalLight`, `MeshRenderable`, `SpriteRenderable`. Every stored entity has `EntityGuid`; human-readable label is **only** the entity-level **`name`** field, mirrored at runtime with Flecs **`Name`** (do not duplicate a `name` entry inside `components`).

**Never on disk:** `LocalToWorld` — derived **deterministically after load** before gameplay relies on it (and updated each frame by systems as today). Validators should reject a `local_to_world` component key in canonical files unless a future `registry_version` explicitly adds it.

**Runtime-only (not authored as entity blobs):** e.g. `EngineInputSnapshot`, `FpsCameraController` state. **Camera pose** for round-trip must live in **`Transform`**; save paths must **sync controller orientation into `Transform`** before emitting JSON so reload matches what the user saw. Controller tuning stays code-owned until a deliberate authoring type exists.

---

## Evolution and versioning

- **`registry_version`:** Single monotonic integer for the **JSON envelope + every registered component codec** shipped together. Breaking any payload or envelope rule bumps it; use **`teng-scene-tool migrate`** (and repo-wide batch updates) so files stay consistent.
- **`binary_format_version`:** Separate from `registry_version`; identifies cooked blob **layout** only. Cooked data still carries or implies a compatible semantic/registry generation; reject unknown pairs clearly.
- **JSON vs binary drift:** Cook path must stay semantically aligned with JSON codecs for the same `registry_version`. Prefer **parity tests** (cook → dump → compare to canonical JSON) until/unless JSON and binary are generated from one schema.

---

## Implementation today

- **Code:** `SceneSerialization.hpp` / `SceneSerialization.cpp` — JSON load/save, validation envelope, cook/dump/migrate hooks, `k_scene_registry_version`, `k_scene_binary_format_version`.
- **Components:** Flecs types in `SceneComponents.hpp`; extraction in `RenderSceneExtractor.cpp` stays separate from on-disk payloads (do not serialize `RenderScene` or GPU structures).

---

## Direction: composable registration

**Problem:** New serializable components are still wired through a **central** codec table and parallel **cooked bitmask** definitions in the serialization TU. That does not scale to **game-defined** components or editor-driven registration.

**Target:** Engine core, game modules, and eventually editor/UI contribute **one registration site per type** (macro, static initializer, generated registrar, Flecs metadata bridge, or small schema — choice is an implementation task). Each registration should bind: stable string key, JSON validate/serialize/deserialize, optional migrator, cook flags/binary codec — so keys and binary bits cannot drift.

Until that lands, any new authored component still requires updating serialization (and generator/demo assets if applicable) in the same change, or an explicit **non-serialized experimental** note.

---

## Tooling and linkage

- **`teng-scene-tool`** / **`teng_scene_tool_lib`:** Validate, migrate, cook, dump — **GPU-free** (no `teng_gfx` / window). Built by default verification per `AGENTS.md`.
- **Linkage:** Follow [`library_linkage_architecture_plan.md`](library_linkage_architecture_plan.md); keep Flecs scoped to `teng_scene`.

---

## Resolved decisions (stable)

| Topic | Decision |
|-------|----------|
| Canonical scene text | JSON only (`*.tscene.json`). |
| JSON library | `nlohmann/json` for scene bytes. |
| Entity label | Flecs `Name` at runtime; disk `name` on entity record only. |
| `LocalToWorld` | Derived after load; not authored. |
| FPS camera | Pose round-trips via `Transform`; controller blob not on disk. |
| Endianness (cooked) | Little-endian; big-endian unsupported. |
| Pre-hierarchy transform | `Transform` is parent-local; implicit world root until hierarchy lands in ECS/format. |
