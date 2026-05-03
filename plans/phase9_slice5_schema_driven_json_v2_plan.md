# Phase 9 Slice 5: Schema-driven JSON v2 implementation plan

**Status:** Reviewed implementation plan for Phase 9 Slice 5. Ready for implementation outline/code
work in a follow-up turn.

Parent plans:

- [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md)
- [`component_schema_authoring_implementation_plan.md`](component_schema_authoring_implementation_plan.md)
- [`component_schema_authoring_model.md`](component_schema_authoring_model.md)
- [`scene_serialization_design.md`](scene_serialization_design.md)

## Goal

Replace the current hand-maintained JSON scene component table with canonical JSON v2 driven by the
frozen component registry and explicit per-component serialization bindings.

This slice should leave JSON load/save strict, deterministic, GPU-free, and ready for later cooked v2
and authoring work. It should not preserve runtime support for old v1 scene JSON.

## Non-goals

- Cooked scene v2. Remove or disable the existing cooked scene CLI/API surface for this slice rather
  than preserving v1 cooked behavior.
- Scene migration tooling. JSON v2 is the first schema-backed scene format; do not keep `migrate`
  unless there is a real source/target migration to implement later.
- Test-module extension proof. That is Slice 6.
- Authoring `SceneDocument`, transactions, dirty tracking, or demo generation. Those are later slices.
- Editor UI, inspector UI, prefab behavior, hierarchy behavior, scripting, or player saves.
- Tolerant unknown-component preservation. Runtime/editor/tool load stays strict by default.

## Relevant current code

Current schema foundation:

- `src/core/ComponentRegistry.*` owns frozen component schema metadata, storage policy, visibility,
  field metadata/defaults, sorted component order, and stable component IDs. It validates module
  registrations during freeze but currently does not retain frozen module records.
- `src/engine/scene/CoreComponentRegistrar.*` registers core schema metadata through
  `register_core_components(ComponentRegistryBuilder&)`.
- `src/engine/scene/SceneComponentContext.*` owns Flecs registration/apply-on-create bindings and is
  deliberately separate from schema metadata.
- `src/engine/scene/Scene.*` and `SceneManager.*` currently only retain `FlecsComponentContext`.

Current serialization:

- `src/engine/scene/SceneSerialization.*` still owns the central JSON `ComponentCodec` table,
  short component keys such as `transform`, v1 `registry_version`, v1 cooked component masks, and
  handwritten component parse/serialize functions.
- `serialize_schema_to_json(const core::ComponentRegistry&)` currently sits in an unnamed namespace in
  `SceneSerialization.cpp`. It serializes registry metadata for tooling/debugging, not canonical scene
  files. That location hides a reusable schema-introspection operation inside a scene-file codec.
- `validate_scene_file`, `cook_scene_file`, `dump_cooked_scene_file`, `migrate_scene_file`, and
  `teng-scene-tool` call the current `SceneSerialization` API without an explicit
  `ComponentRegistry`.

Important gap:

- `ComponentRegistry` currently contains field metadata but not member accessors or type-erased
  runtime read/write hooks. A pure registry walk can validate JSON field shape/defaults, but it cannot
  serialize actual Flecs component values or apply loaded payloads to entities without additional
  per-component bindings.

## Target architecture

Slice 5 should split the responsibilities into three GPU-free pieces inside `teng_scene`:

1. **Component schema JSON metadata export**
   - New files: `src/engine/scene/ComponentSchemaJson.hpp` and `.cpp`.
   - Public API consumes `const core::ComponentRegistry&`.
   - Owns the current `serialize_schema_to_json` behavior under
     `serialize_component_schema_to_json`.
   - Stays separate from canonical scene file load/save.
   - Do not move this into `teng_core` unless the repo deliberately accepts `nlohmann/json` as a core
     dependency. The preferred Slice 5 boundary keeps `core` registry pure and puts JSON rendering in
     the scene/tool layer.

2. **Scene serialization context**
   - New files: `src/engine/scene/SceneSerializationContext.hpp` and `.cpp`, or equivalent.
   - Constructed from a frozen `ComponentRegistry`.
   - Holds registry reference plus per-component JSON/Flecs bindings keyed by namespaced component key.
   - Validates that every built-in `Authored` registry component has a binding when needed.
   - Does not replace `FlecsComponentContext`; it is a sibling context for load/save.

3. **JSON v2 serializer**
   - `SceneSerialization.cpp` becomes scene file orchestration: read/write files, validate envelope,
     enumerate entities, call `SceneSerializationContext`, produce canonical JSON v2.
   - It should no longer be the source of truth for component keys, field lists, component versions,
     or storage policy.

Longer-term codegen can generate both schema metadata and typed serialization bindings from one
component declaration. For Slice 5, handwritten built-in component bindings are acceptable only if
they live beside scene component registration and do not form a separate central engine serialization
table.

## Public API shape

Prefer explicit registry/context parameters over hidden globals:

```cpp
struct SceneSerializationContext;

[[nodiscard]] Result<SceneSerializationContext> make_builtin_scene_serialization_context(
    const core::ComponentRegistry& registry);

[[nodiscard]] Result<nlohmann::json> serialize_scene_to_json(
    const Scene& scene, const SceneSerializationContext& serialization);

[[nodiscard]] Result<void> deserialize_scene_json(
    SceneManager& scenes, const SceneSerializationContext& serialization, const nlohmann::json& json);

[[nodiscard]] Result<void> save_scene_file(
    const Scene& scene, const SceneSerializationContext& serialization, const std::filesystem::path& path);

[[nodiscard]] Result<SceneLoadResult> load_scene_file(
    SceneManager& scenes, const SceneSerializationContext& serialization, const std::filesystem::path& path);

[[nodiscard]] Result<void> validate_scene_file(
    const SceneSerializationContext& serialization, const std::filesystem::path& path);
```

Compatibility overloads without a serialization context should be removed during this slice unless
there is a short-lived migration need inside the same branch. Runtime startup already builds a frozen
registry; retain it durably and pass it to both Flecs and serialization contexts.

Keep `SceneManager` focused on scene lifetime. It should retain only `FlecsComponentContext`, while
serialization APIs receive `SceneSerializationContext` explicitly. Do not introduce a broader
`SceneRuntimeContext` in Slice 5.

## Component binding design

Add explicit typed bindings, registered in new built-in scene serialization files:

```cpp
struct SceneComponentJsonBinding {
  std::string_view component_key;
  bool (*has_component)(flecs::entity entity);
  Result<nlohmann::json> (*serialize_component)(flecs::entity entity);
  Result<void> (*deserialize_component)(flecs::entity entity, const nlohmann::json& payload);
};
```

The binding builder should verify:

- `component_key` exists in the frozen registry.
- Authored components have serialize/deserialize functions.
- Runtime-only components may have debug serialization bindings later, but canonical scene load/save
  rejects them while their storage policy is not `Authored`.
- Binding registration order does not affect canonical output.

The old `ComponentCodec` table in `SceneSerialization.cpp` should be deleted or reduced to temporary
private scaffolding with clear retirement in the same slice. Do not leave two authoritative component
key systems.

Preferred built-in binding registration API:

```cpp
void register_builtin_scene_serialization(SceneSerializationContextBuilder& builder);
```

## Schema-driven field validation

Field validation should be registry-driven and independent from typed deserialization where possible:

- Envelope validation checks top-level v2 shape and unknown keys.
- Schema metadata validation checks `schema.required_modules` and `schema.required_components`
  against the frozen registry.
- Component validation checks each component key exists, is `Authored`, and uses the supported schema
  version.
- Field validation walks `FrozenComponentRecord::fields` in declaration order and requires complete
  payloads. Missing fields are rejected even when the schema has defaults.
- Field JSON type checks come from `ComponentFieldKind`.
- `AssetId` fields validate text syntax only; asset existence/kind is project validation for a later
  layer.
- Enum fields validate stable enum value keys.
- Component-local validation hooks should run after a typed component is parsed, when available.

Typed deserialization still sets actual C++ component values on the entity. The typed binding can rely
on prior schema field validation for shape, but it should still return a `Result` for malformed data
and impossible mismatch during migration.

## JSON v2 envelope requirements

Canonical save emits:

```json
{
  "scene_format_version": 2,
  "schema": {
    "required_modules": [
      { "id": "teng.core", "version": 1 }
    ],
    "required_components": {
      "teng.core.transform": 1
    }
  },
  "scene": {
    "name": "Untitled Scene"
  },
  "entities": [
    {
      "guid": "000000000000007b",
      "components": {
        "teng.core.transform": {
          "translation": [0, 0, 0],
          "rotation": [1, 0, 0, 0],
          "scale": [1, 1, 1]
        }
      }
    }
  ]
}
```

Rules:

- `scene_format_version` is required and must be `2`.
- `registry_version` is removed from JSON v2.
- `schema.required_modules` includes exactly the modules required by serialized components, not every
  module registered in the process.
- `schema.required_components` includes exactly the components used by the file and records current
  schema versions.
- `schema.required_components` values are component schema versions, not embedded component schemas.
- Registry fingerprints are not part of JSON v2 in Slice 5; cooked/cache work may add one later.
- `scene.name` is required and non-empty. New/imported scenes should receive a default name before
  save.
- Entity `guid` is a fixed-width lowercase hex string for the current `uint64_t` `EntityGuid` value.
- Entity `name` is optional, but if present it must be non-empty.
- Entity `components` is required and may be `{}` for an entity with no authored components.
- `EntityGuidComponent` and `Name` do not appear in `components`.
- Component map keys are namespaced registry keys, e.g. `teng.core.transform`.
- Vectors use JSON arrays. Quaternions use `[w, x, y, z]` to match the current GLM-facing component
  convention.
- Canonical payloads include every declared field. No default elision in Slice 5, and strict load does
  not treat missing fields as implicit defaults.
- Unknown top-level, `schema`, `scene`, entity, component, and field keys are rejected by strict load.

## Registry fingerprint

Do not implement a registry fingerprint in Slice 5. JSON v2 load correctness comes from explicit
`scene_format_version`, `schema.required_modules`, `schema.required_components`, component keys,
component schema versions, and registry-driven field validation.

Cooked scene v2 or asset/cook cache invalidation may introduce a deterministic registry fingerprint
later, once the invalidation semantics are concrete.

## Canonical ordering

Save order must be deterministic:

- entities sorted by `EntityGuid::value` ascending
- components sorted by namespaced component key
- fields emitted in `FrozenComponentRecord::fields` declaration order
- `schema.required_modules` sorted by module id
- `schema.required_components` sorted by component key

Use `nlohmann::ordered_json` at the canonical output boundary so the written file preserves the
serializer's intended object order. Load should not reject non-canonical object order; JSON object
order is normalized by save.

Use normal `nlohmann` numeric serialization for `float` values in Slice 5. Do not add custom float
formatting unless tests prove round-trip instability.

## Runtime ownership changes

`Engine::init()` currently creates `ComponentRegistry` as a local, then builds `FlecsComponentContext`.
Slice 5 should retain the frozen registry for the process lifetime:

- Add `std::unique_ptr<core::ComponentRegistry> frozen_component_registry_` to `Engine`.
- Add `std::unique_ptr<SceneSerializationContext> scene_serialization_ctx_`.
- Build registry once, then build Flecs and serialization contexts from it.
- Pass `scene_serialization_ctx_` into load/save calls.

Tests and tools should use a shared helper that builds:

- frozen built-in component `ComponentRegistry`
- `FlecsComponentContext`
- `SceneSerializationContext`

Avoid rebuilding registries inside individual load/save calls. That would hide project/module context
and block Slice 6 extension tests.

## Tooling boundary

`teng-scene-tool` must stay GPU-free:

- It should build/freeze the built-in component registry in the CLI process.
- It should build the scene serialization context without creating a window, renderer, asset service,
  or Flecs world unless the subcommand actually loads into ECS.
- `validate` should validate JSON v2 through the frozen registry and serialization context.
- Remove `migrate` until there is a real migration source/target.
- Remove `cook` and `dump` from the supported CLI surface, or leave them as commands that fail with a
  clear "cooked scenes are not supported until Phase 9 Slice 7" diagnostic. Do not preserve v1 cooked
  round trips for first-party tests.

## Files and targets

Likely additions:

- `src/engine/scene/ComponentSchemaJson.hpp`
- `src/engine/scene/ComponentSchemaJson.cpp`
- `src/engine/scene/SceneSerializationContext.hpp`
- `src/engine/scene/SceneSerializationContext.cpp`
- `src/engine/scene/BuiltinSceneSerialization.hpp`
- `src/engine/scene/BuiltinSceneSerialization.cpp`

Likely changes:

- `src/core/ComponentRegistry.hpp/.cpp`
  - add frozen module records to the registry and expose a deterministic module list/find API.
- `src/engine/scene/CoreComponentRegistrar.hpp/.cpp`
  - keep schema/Flecs registration here for this slice; broader naming cleanup is future work.
- `src/engine/scene/BuiltinSceneSerialization.hpp/.cpp`
  - add `register_builtin_scene_serialization(SceneSerializationContextBuilder&)`.
- `src/engine/scene/SceneSerialization.hpp/.cpp`
  - switch APIs to explicit serialization context.
  - implement JSON v2 envelope.
  - remove v1 `registry_version` as runtime load path.
  - remove central `ComponentCodec` authority.
- `src/engine/scene/SceneManager.*`
  - likely no behavioral changes if serialization context is passed explicitly.
- `src/engine/Engine.hpp/.cpp`
  - keep frozen registry and serialization context alive.
  - pass context to startup scene loading.
- `apps/teng-scene-tool/main.cpp`
  - build the built-in registry and serialization context for `validate`.
  - remove or explicitly disable `migrate`, `cook`, and `dump`.
- `tests/smoke/TestHelpers.*`
  - return or own both Flecs and serialization contexts.
- `tests/smoke/SceneSerializationSmokeTest.cpp`
  - rewrite fixtures to JSON v2 namespaced keys.
- `tests/core/ComponentRegistryTests.cpp`
  - add module retention tests.
- `src/CMakeLists.txt`
  - add new source files to `TENG_SCENE_SOURCES`.

Avoid adding rendering, platform, or `teng_engine_runtime` dependencies to `teng_scene_tool_lib`.

## Implementation sequence

1. **Freeze module metadata**
   - Add deterministic frozen module records to `ComponentRegistry`.
   - Use the frozen module list for JSON v2 `required_modules` validation and full schema export.
   - Test module sorting and duplicate-version behavior remains unchanged.

2. **Move schema metadata JSON export**
   - Move `serialize_schema_to_json` out of the unnamed namespace in `SceneSerialization.cpp`.
   - Put it in `ComponentSchemaJson.*` as `serialize_component_schema_to_json`.
   - Add or update a focused test that exports built-in component schema metadata.
   - This is a cleanup step, not the scene JSON v2 implementation.

3. **Add serialization context and typed bindings**
   - Add `SceneSerializationContextBuilder`.
   - Register built-in typed bindings in `BuiltinSceneSerialization.*`.
   - Builder validates binding/key/storage invariants against the frozen registry.
   - Keep bindings keyed by namespaced component key.

4. **Implement schema-driven payload validation**
   - Add field-kind JSON validators.
   - Validate complete fields in declaration order.
   - Reject unknown fields, missing fields, unknown component keys, unsupported schema versions,
     missing/unsupported modules, unused required modules/components, and non-`Authored` payload
     components.
   - Return structured diagnostics internally where practical; existing `Result<void>` APIs may render
     diagnostics to strings until broader `Result` migration happens.

5. **Implement JSON v2 save**
   - Serialize entity metadata.
   - Enumerate authored components through serialization context bindings.
   - Use registry metadata to emit `schema.required_components` and `schema.required_modules`.
   - Emit complete field payloads and deterministic ordering.

6. **Implement JSON v2 load**
   - Validate envelope, schema dependency summary, entities, fields, and typed payload shape before
     creating the scene.
   - Create the scene only after validation succeeds.
   - Create entities by GUID/name.
   - Apply typed component payloads through bindings.
   - Let add-on-create defaults run first, then overwrite authored components from payload.
   - Recompute `LocalToWorld` after load.
   - If an apply-time failure still occurs, destroy the partially created scene before returning the
     error.

7. **Update runtime, tool, and tests**
   - Update `Engine` startup ownership.
   - Update `teng-scene-tool`.
   - Convert smoke fixtures to v2.
   - Delete or regenerate v1 fixture content.

8. **Retire v1 central JSON authority**
   - Remove short-key component codecs from scene JSON load/save.
   - Remove or disable cooked v1 code and CLI exposure; do not keep cooked v1 round-trip tests.
   - Remove `migrate` until there is an actual migration target/source.
   - Use `rg` to confirm old short component keys are not accepted by JSON scene load.

## Required tests

Add or update tests for:

- Built-in JSON v2 round trip with `Transform`, `Camera`, `DirectionalLight`, `MeshRenderable`, and
  `SpriteRenderable`.
- Strict v1 rejection: `registry_version` files fail runtime/tool load.
- Unknown top-level key rejection.
- Unknown `schema` key rejection.
- Unknown `scene` key rejection.
- Empty/missing `scene.name` rejection.
- Entity `guid` string validation and malformed GUID rejection.
- Empty present entity `name` rejection.
- Required `entities[].components` object, including `{}` for empty entities.
- Exact/minimal `schema.required_modules` and `schema.required_components` validation, including
  unused required module/component rejection.
- Unknown component key rejection.
- Unsupported module version rejection.
- Unsupported component schema version rejection.
- Runtime-only component rejection, e.g. `teng.core.local_to_world`.
- Missing field rejection, including fields that have schema defaults.
- Unknown field rejection.
- Wrong field type rejection for bool, float, vector, quaternion, `AssetId`, and enum when present.
- Invalid `AssetId` syntax rejection without checking asset existence.
- Complete default field emission.
- Deterministic entity/component/field ordering.
- Load accepts non-canonical object order and save normalizes order.
- Component schema metadata JSON export from `ComponentSchemaJson`.
- Tool validate failure path for a malformed v2 file.

Existing smoke tests should continue covering render extraction after load.

## Validation command

Use the repository verification script from root:

```bash
./scripts/agent_verify.sh
```

During development, useful narrower checks are:

```bash
cmake --build --preset Debug --target teng_engine_tests engine_scene_smoke teng-scene-tool
./build/Debug/bin/teng_engine_tests
./build/Debug/bin/engine_scene_smoke
./build/Debug/bin/teng-scene-tool validate resources/scenes/demo_cube.tscene.json
```

If shader files are not touched, `./scripts/agent_verify.sh --quick` is acceptable for intermediate
iterations, but the final slice should run the full verification command.

## Migration scaffolding and retirement criteria

Temporary scaffolding allowed:

- A `SceneSerializationContext` with handwritten built-in typed bindings.
- A rendered diagnostic bridge from structured diagnostics to `Result` strings.

Retire by end of Slice 5:

- `serialize_schema_to_json` hidden in `SceneSerialization.cpp`.
- Runtime/editor/tool acceptance of v1 `registry_version` scene JSON.
- Short component keys in canonical scene load/save.
- The central JSON `ComponentCodec` table as authoritative component vocabulary.
- Public `migrate`, `cook`, and `dump` behavior for v1 scenes/cooked scenes, unless left only as
  explicit unsupported diagnostics.

Retire by Slice 7:

- Any temporary unsupported cooked-scene CLI/API stubs introduced to keep Slice 5 honest.

Retire by future codegen/schema consolidation:

- Handwritten built-in typed serialization binding functions, once component declarations can generate
  field accessors and typed serializers.

## Risks and tradeoffs

- **Registry metadata is not enough for runtime serialization.** Without type-erased accessors or
  bindings, the implementation would recreate the old central table. Keeping bindings near component
  registration is the near-term compromise.
- **`core` JSON dependency creep.** Moving schema JSON export into `core` would make registry
  introspection convenient but broadens `teng_core`. Keeping JSON rendering in `teng_scene` preserves
  the current layering.
- **Default-on-create overwrite semantics.** Loading should create entities normally after validation,
  then apply authored payloads. Tests must prove authored `Transform` values overwrite default
  `Transform`.
- **Cooked path removal.** JSON v2 will not match the current cooked v1 format. Removing or disabling
  cooked commands in Slice 5 may temporarily reduce CLI surface, but avoids silently preserving invalid
  cooked data.
- **Object ordering with `nlohmann::json`.** Canonical human-readable ordering should use
  `ordered_json` at the output boundary. Load should not reject files only because object order differs.
- **Diagnostics migration size.** Full structured diagnostic plumbing could expand the slice. Keep
  stable diagnostic codes internally, but allow public `Result` string rendering if that keeps the
  slice mergeable.
- **Naming debt.** `register_core_components` and `src/core/ComponentRegistry.*` are not ideal long-term
  names/locations for scene component schema work. Slice 5 should avoid mixing broad naming migration
  into JSON v2 unless it becomes necessary.

## Review decisions

- `ComponentRegistry` retains frozen module records. Scene files still emit only the exact modules
  required by serialized components.
- `SceneSerializationContext` is passed explicitly to serialization APIs. No `SceneRuntimeContext` in
  Slice 5.
- `cook`, `dump`, and `migrate` are removed or disabled until there is cooked v2 or real migration
  work. V1 cooked behavior is not preserved for first-party tests.
- Canonical output uses `nlohmann::ordered_json`; load does not reject non-canonical object order.
- Registry fingerprint is deferred until cooked/cache invalidation needs it.
- Full component schema export is separate from scene files and lives in `ComponentSchemaJson.*`.
- Scene files embed only `schema.required_modules` and `schema.required_components`, both exact and
  minimal.
- `schema.components` is renamed to `schema.required_components`.
- Entity component payloads remain under `entities[].components`, which is required and may be empty.
- `scene.name` is required and non-empty.
- Entity `name` is optional, but non-empty when present.
- Entity `guid` is a fixed-width lowercase hex string for the current `uint64_t` ID.
- Vectors/quaternions stay arrays; quaternions use `[w, x, y, z]`.
- Missing component fields are rejected even when defaults exist.
- Runtime-only/debug components are rejected by canonical JSON v2; no debug export path in Slice 5.
- Built-in JSON bindings live in `BuiltinSceneSerialization.*`, not `src/core`.
- `scene_format_version` remains the top-level format version key.
