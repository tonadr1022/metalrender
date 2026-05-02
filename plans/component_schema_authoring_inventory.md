# Component schema authoring inventory

**Status:** Slice 0 current-state inventory for Phase 9. Sequencing plan:
[`component_schema_authoring_implementation_plan.md`](component_schema_authoring_implementation_plan.md).

This document tracks the concrete code paths that the component schema and authoring overhaul must
replace. It is intentionally implementation-facing and may be retired when Phase 9 cleanup is complete.

## Current central serialization sites

Primary files:

- `src/engine/scene/SceneSerialization.hpp`
- `src/engine/scene/SceneSerialization.cpp`

Current public scene serialization surface:

- `serialize_scene_to_json`
- `deserialize_scene_json`
- `save_scene_file`
- `load_scene_file`
- `validate_scene_file`
- `cook_scene_to_memory`
- `cook_scene_file`
- `dump_cooked_scene_to_json`
- `dump_cooked_scene_file`
- `migrate_scene_file`

Current version symbols:

- `k_scene_registry_version = 1`
- `k_scene_binary_format_version = 1`

Retirement target:

- Replace the single `registry_version` semantic with `scene_format_version`, module metadata,
  per-component schema versions, and cooked `binary_format_version`.

## Current component payload logic

All authored component payload behavior is centralized in `SceneSerialization.cpp`.

Current parse/JSON helpers:

- `parse_transform`
- `transform_json`
- `parse_camera`
- `camera_json`
- `parse_directional_light`
- `directional_light_json`
- `parse_mesh_renderable`
- `mesh_renderable_json`
- `parse_sprite_renderable`
- `sprite_renderable_json`
- shared field helpers such as `required_float`, `required_int`, `required_bool`, `required_vec3`,
  `required_vec4`, and `parse_asset_id_field`

Current validation wrappers:

- `validate_transform_payload`
- `validate_camera_payload`
- `validate_directional_light_payload`
- `validate_mesh_renderable_payload`
- `validate_sprite_renderable_payload`

Current serialization callbacks:

- `serialize_transform_payload`
- `serialize_camera_payload`
- `serialize_directional_light_payload`
- `serialize_mesh_renderable_payload`
- `serialize_sprite_renderable_payload`

Current deserialization callbacks:

- `deserialize_transform_payload`
- `deserialize_camera_payload`
- `deserialize_directional_light_payload`
- `deserialize_mesh_renderable_payload`
- `deserialize_sprite_renderable_payload`

Current central codec table:

- `ComponentCodec`
- `component_codecs()`
- `find_component_codec()`

Current v1 component keys:

- `transform`
- `camera`
- `directional_light`
- `mesh_renderable`
- `sprite_renderable`

Phase 9 replacement:

- Core component schemas with namespaced keys:
  - `teng.core.transform`
  - `teng.core.camera`
  - `teng.core.directional_light`
  - `teng.core.mesh_renderable`
  - `teng.core.sprite_renderable`
- Declarative fields become the source for validation, JSON, cook, defaults, and editor/tool metadata.
- Adding a test-only component outside core must not require editing `SceneSerialization.cpp`.

## Current envelope and strictness logic

Current envelope parsing and validation is in:

- `parse_json_file`
- `ensure_object`
- `ensure_allowed_keys`
- `parse_guid`
- `parse_entity_records`
- `validate_envelope`

Current v1 top-level keys:

- `registry_version`
- `scene`
- `entities`

Current entity keys:

- `guid`
- `name`
- `components`

Current special runtime-only rejection:

- `local_to_world` is rejected by explicit string check in `validate_envelope`.

Phase 9 replacement:

- JSON v2 envelope from [`scene_serialization_design.md`](scene_serialization_design.md).
- Strictness uses frozen registry lookup and structured diagnostics.
- Runtime-only rejection comes from component storage policy, not a single hard-coded key check.

## Current cooked scene sites

Cooked scene implementation is centralized in `SceneSerialization.cpp`.

Current fixed identity model:

- `enum ComponentBit`
- `k_transform_bit`
- `k_camera_bit`
- `k_directional_light_bit`
- `k_mesh_renderable_bit`
- `k_sprite_renderable_bit`
- `CookEntity::component_mask`
- `component_bit()`

Current cooked component blob functions:

- `write_component_blob`
- `read_component_blob`
- `write_transform_blob`
- `read_transform_blob`
- inline key branches for camera, directional light, mesh renderable, and sprite renderable

Current cooked file flow:

- `cook_scene_to_memory`
- `cook_scene_file`
- `dump_cooked_scene_to_json`
- `dump_cooked_scene_file`

Current cooked file constraints:

- `k_cooked_magic`
- `k_cooked_header_size`
- little-endian only
- version pair checks against `k_scene_binary_format_version` and `k_scene_registry_version`

Phase 9 replacement:

- Stable component IDs derived from namespaced component keys.
- Component schema version carried per component record.
- Field blobs encoded from schema declaration order.
- No hand-assigned global component bit enum as the identity model.
- Cook/dump parity must include the test-only registered component.

## Current runtime scene component registration

Current file:

- `src/engine/scene/Scene.cpp`

Current manual component list:

- `Scene::register_components()`
- `EntityGuidComponent`
- `Name`
- `Transform`
- `LocalToWorld`
- `Camera`
- `FpsCameraController`
- `EngineInputSnapshot`
- `DirectionalLight`
- `MeshRenderable`
- `SpriteRenderable`

Related default-on-create behavior:

- `Scene::create_entity()` always sets `EntityGuidComponent`.
- `Scene::create_entity()` always sets `Transform`.
- `Scene::create_entity()` always sets `LocalToWorld`.
- `Name` is set when entity name is non-empty.

Phase 9 replacement:

- `Scene` and `SceneManager` require a frozen registry/context.
- Flecs component registration is driven by the frozen registry.
- Default-on-create policy is expressed by schema/context.
- `EntityGuidComponent` and `Name` remain identity/document metadata, not normal authored component
  payloads.

## Current scene generation sites

Primary file:

- `scripts/generate_demo_scene_assets.py`

Current responsibilities:

- Generates demo model sidecars.
- Deletes old `demo_*.tscene.toml`.
- Deletes old `demo_*.tscene.json`.
- Constructs canonical scene JSON by hand.
- Emits `registry_version = 1`.
- Emits short component keys:
  - `transform`
  - `camera`
  - `directional_light`
  - `mesh_renderable`
- Writes `resources/scenes/demo_cube.tscene.json` as a copy of demo 00.

Specific handwritten scene JSON helpers:

- `entity_record`
- `write_scene`
- direct component dict construction in `write_scene`
- `json.dumps(scene, indent=2, sort_keys=True)`

Phase 9 replacement:

- Python may continue non-scene asset orchestration if useful.
- Python must not construct canonical scene component JSON.
- Demo scenes should be generated by C++ schema-aware authoring code and saved through the canonical
  serializer.

## Current checked-in scene resources

Current v1 JSON scene files:

- `resources/scenes/demo_00_cube.tscene.json`
- `resources/scenes/demo_01_cube_grid.tscene.json`
- `resources/scenes/demo_02_random_cubes.tscene.json`
- `resources/scenes/demo_03_suzanne.tscene.json`
- `resources/scenes/demo_04_sponza.tscene.json`
- `resources/scenes/demo_05_chessboard.tscene.json`
- `resources/scenes/demo_cube.tscene.json`

Current project startup reference:

- `resources/project.toml` points at `resources/scenes/demo_00_cube.tscene.json`.

Phase 9 replacement:

- Regenerate or replace checked-in scenes as JSON v2.
- Runtime v1 support is not required.
- Keep project startup scene path valid after regeneration.

## Current scene tool sites

Primary files:

- `apps/teng-scene-tool/main.cpp`
- `src/engine/scene/SceneValidate.cpp`

Current CLI commands:

- `validate`
- `migrate`
- `cook`
- `dump`

Current CLI error model:

- `Result<void>` with string rendering to stderr.

Phase 9 replacement:

- CLI should render structured diagnostics where validation/freeze reports provide them.
- `migrate` remains future migration entry point, but v1 preservation is not required unless a real
  authored-content need appears.
- Tool remains GPU-free.

## Current tests and fixtures

Primary smoke target:

- `engine_scene_smoke`

Current scene serialization smoke:

- `tests/smoke/SceneSerializationSmokeTest.cpp`
- Builds v1 JSON text by hand in `valid_scene_text`.
- Uses short component keys and `registry_version`.
- Exercises:
  - `load_scene_file`
  - render extraction after load
  - `save_scene_file`
  - round-trip load
  - `cook_scene_file`
  - `dump_cooked_scene_file`
  - `validate_scene_file`
  - rejection of `local_to_world`

Current generated scene assets smoke:

- `tests/smoke/GeneratedSceneAssetsSmokeTest.cpp`
- Loads:
  - `resources/scenes/demo_00_cube.tscene.json`
  - `resources/scenes/demo_01_cube_grid.tscene.json`
- Checks camera/light/mesh extraction counts.

Current scene foundation/render extraction smoke:

- `tests/smoke/SceneSmokeTest.cpp`
- Creates scenes/components directly through current `Scene` API.
- Exercises default `Transform`/`LocalToWorld`, render extraction, and FPS camera system.

Phase 9 replacement/additions:

- Add diagnostics tests in Slice 1 (done).
- Add registry freeze diagnostics tests in Slice 2 (done, `tests/core/ComponentRegistryTests.cpp`).
- Update serialization smoke to JSON v2 in Slice 5.
- Add test-only component extension proof in Slice 6.
- Update cook/dump parity for schema-driven cooked v2 in Slice 7.
- Update generated scene assets smoke after C++ schema-aware generation in Slice 9.

## Retirement checklist

Remove or replace before Phase 9 exits:

- [ ] `k_scene_registry_version` as the single scene/component semantic version.
- [ ] v1 `registry_version` JSON envelope.
- [ ] short component keys in canonical scene files.
- [ ] `ComponentBit` enum as global cooked component identity.
- [ ] `CookEntity::component_mask` as fixed global-bit identity.
- [ ] `ComponentCodec` as a hand-maintained central component list.
- [ ] handwritten per-component JSON parser/serializer functions as the source of truth.
- [ ] handwritten per-component cooked key branches as the source of truth.
- [ ] manual `Scene::register_components()` list as authoritative Flecs registration.
- [ ] Python construction of canonical scene component JSON.
- [ ] checked-in v1 demo scene resources.

## Slice 1 completion

Core diagnostics landed as the first Phase 9 implementation slice.

Implemented code targets:

- `src/core/Diagnostic.hpp`
- `src/core/Diagnostic.cpp`
- `tests/core/DiagnosticTests.cpp`
- CMake/test wiring through `teng_core_tests` and the default verification script.

`Result<T>` was intentionally not migrated globally. The diagnostics type is available for registry
freeze and scene validation reports in later slices.

## Slice 2 completion

Registry builder/freeze and core registrar landed without wiring `Scene`, JSON, cook, or demo
generation to the frozen registry.

Implemented code targets:

- `src/core/ComponentRegistry.hpp`, `src/core/ComponentRegistry.cpp` — builder, frozen registry,
  `stable_component_id_v1`, freeze diagnostics (`schema.*` codes).
- `src/engine/scene/CoreComponentRegistrar.hpp`, `src/engine/scene/CoreComponentRegistrar.cpp` —
  `register_core_components`.
- `tests/core/ComponentRegistryTests.cpp` — freeze failures and stable codes; deterministic stable IDs.

## Slice 4 handoff

Slice 3 added declarative fields, visibility, defaults, asset/enum metadata, and hooks on the frozen path
(without JSON/cook/Flecs consumption). Slice 4 introduces registry-driven Flecs registration and an
explicit scene/component context.

Retain existing `SceneSerialization.*`, `Scene::register_components()`, and v1 scene resources until
their planned slices replace them.
