# Component schema authoring inventory

**Status:** Phase 9 retirement checklist. This is intentionally implementation-facing and should be
deleted or reduced to a short historical note during Slice 10 cleanup.

Sequencing plan: [`component_schema_authoring_implementation_plan.md`](component_schema_authoring_implementation_plan.md).

## Purpose

Track old serialization/cook/demo paths that must be replaced by the component schema and authoring
overhaul. Do not add slice diary entries here; future slices should update checklist state and remove
retired items.

## Active retirement checklist

Remove or replace before Phase 9 exits:

- [ ] `k_scene_registry_version` as the single scene/component semantic version.
- [ ] v1 `registry_version` JSON envelope.
- [ ] short component keys in canonical scene files.
- [ ] `ComponentBit` enum as global cooked component identity.
- [ ] `CookEntity::component_mask` as fixed global-bit identity.
- [ ] `ComponentCodec` as a hand-maintained central component list.
- [ ] handwritten per-component JSON parser/serializer functions as the source of truth.
- [ ] handwritten per-component cooked key branches as the source of truth.
- [x] manual `Scene::register_components()` list as authoritative Flecs registration.
- [ ] Python construction of canonical scene component JSON.
- [ ] checked-in v1 demo scene resources.

## Current code to inspect

Use this section as a starting map only. Inspect the code before implementing a slice.

| Area | Current files / symbols | Replacement direction |
|------|-------------------------|-----------------------|
| Scene serialization API | `src/engine/scene/SceneSerialization.*`; `serialize_scene_to_json`, `deserialize_scene_json`, `save_scene_file`, `load_scene_file`, `validate_scene_file` | JSON v2 from frozen `ComponentRegistry` and structured diagnostics |
| JSON component payloads | `SceneSerialization.cpp`; `ComponentCodec`, `component_codecs()`, `find_component_codec()`, per-component parse/serialize helpers | Schema field traversal using namespaced component keys |
| JSON envelope | `registry_version`, `scene`, `entities`; hard-coded runtime-only rejection for `local_to_world` | v2 envelope from `scene_serialization_design.md`; storage-policy-driven rejection |
| Cooked scenes | `cook_scene_to_memory`, `cook_scene_file`, `dump_cooked_scene_to_json`, `dump_cooked_scene_file`; `ComponentBit`, component masks, fixed key branches | Stable component IDs from schema keys; schema-versioned field blobs |
| Runtime Flecs registration | `src/engine/scene/SceneComponentContext.*`, `CoreComponentRegistrar.*`, `Scene.*`, `SceneManager.*` | Already split: schema registration via `ComponentRegistry`, Flecs bindings via `FlecsComponentContext` |
| Demo generation | `scripts/generate_demo_scene_assets.py` writes canonical scene JSON by hand | C++ schema-aware authoring/generation path; Python only for non-scene orchestration if useful |
| Checked-in scenes | `resources/scenes/*.tscene.json`, especially demo scenes and `resources/project.toml` startup reference | Regenerate as JSON v2; runtime v1 compatibility is not required |
| Scene tool | `apps/teng-scene-tool/main.cpp`, `src/engine/scene/SceneValidate.cpp` | GPU-free validate/canonicalize/migrate/cook/dump using schema diagnostics |
| Smoke tests | `tests/smoke/SceneSerializationSmokeTest.cpp`, `GeneratedSceneAssetsSmokeTest.cpp`, `SceneSmokeTest.cpp` | Update to JSON v2, schema cook/dump, and C++ generated scene assets |

## Current v1 authored component keys

Short keys to retire from canonical scene files:

- `transform`
- `camera`
- `directional_light`
- `mesh_renderable`
- `sprite_renderable`

Canonical replacements are namespaced schema keys, e.g.:

- `teng.core.transform`
- `teng.core.camera`
- `teng.core.directional_light`
- `teng.core.mesh_renderable`
- `teng.core.sprite_renderable`

## Slice 5 handoff

The registry-only core schema path exists through `register_core_components(ComponentRegistryBuilder&)`.
Flecs bindings are isolated behind `register_flecs_core_components(FlecsComponentContextBuilder&)`.

During Slice 5, route JSON v2 validation/serialization and GPU-free tools through the frozen
`ComponentRegistry` directly. Runtime startup currently builds the registry before freezing
`FlecsComponentContext`; if Slice 5 needs runtime-side schema access, give that registry explicit
durable ownership rather than making `FlecsComponentContext` carry schema data again.
