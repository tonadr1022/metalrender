# Phase 9 Slice 6.5: Component reflection codegen before cooked v2

**Status:** task 1 done.

**Parent plan:** [`component_schema_authoring_implementation_plan.md`](component_schema_authoring_implementation_plan.md).  
**Scene byte contract:** [`scene_serialization_design.md`](scene_serialization_design.md).  
**Next dependent slice:** [`phase9_slice7_cooked_scene_v2_plan.md`](phase9_slice7_cooked_scene_v2_plan.md).

## Purpose

Add a long-term component reflection/codegen layer before Slice 7 so schema registration, Flecs
registration, JSON serialization, cooked v2 field encoding, editor inspection, authoring operations,
and future scripting bindings are all generated from one component metadata source.

This slice should remove the need to hand-maintain parallel component tables in
`CoreComponentRegistrar.cpp`, `BuiltInComponentSerialization.cpp`, and test-module registrars. Slice 7
then consumes generated schema/reflection records instead of introducing new cooked-specific tables.

## Long-term direction

Use an Unreal-style native C++ reflection pipeline:

```text
hand-authored C++ component structs
  + explicit reflection blocks in headers
  -> repo-local header tool
  -> build-dir generated C++ registrar/binding code
  -> registry, Flecs, JSON, cook, editor, authoring, script consumers
```

Do **not** generate component structs in this slice. C++ remains the source of memory layout and hot
simulation behavior. Generated code owns metadata and binding glue.

First scripting VM decision: **Luau**. This slice does not embed Luau, add a VM target, add script
files to scenes, or define script scheduling. It only records script exposure metadata so a later
`teng_script_luau` layer can bind reflected components without redesigning the metadata model.

Hot sim remains native ECS systems. Scripts later are for orchestration, content behavior, triggers,
abilities, quests, and game-specific glue, not Factorio-scale inner loops.

## Current state

- `ComponentRegistry` already freezes module/component schema records and stable component IDs.
- JSON v2 validation and canonical save/load are schema-backed, but concrete component payload access
  is still handwritten through `SceneSerializationContext`.
- Slice 6 proves extension outside core with a test-only component, but still requires a handwritten
  schema registrar, Flecs binding, and JSON binding.
- `ComponentFieldKind::Enum` exists, but current component storage uses authored enum strings in the
  Slice 6 test component. Real `enum class` component fields are not supported by serialization
  bindings yet.
- Existing runtime binding style already uses static function pointers for Flecs registration,
  apply-on-create, JSON serialize/deserialize, and schema validation hooks.

## Design decisions

- Generated files are **build-dir only** under `${CMAKE_BINARY_DIR}/generated/...`; do not commit
  generated C++.
- Use a repo-local Python generator first, because Python is already required by `agent_verify`.
- Use a strict reflection-block DSL in C++ headers. It should be parseable by the first generator and
  stable enough to later feed a Clang/AST-backed tool without changing component authoring semantics.
- Keep the existing public registration entrypoints:
  - `register_core_components(ComponentRegistryBuilder&)`
  - `register_flecs_core_components(FlecsComponentContextBuilder&)`
  - `register_builtin_component_serialization(SceneSerializationContextBuilder&)`
- Generated code may expose type-erased function pointers at registry/context boundaries. Internally,
  generated thunks must remain typed C++.
- Do not use `std::function` for generated static thunks. Function pointers are allocation-free,
  deterministic, and match the current engine style.
- Do not turn reflection into arbitrary `void*` manipulation everywhere. Type erasure is only for
  stable dispatch tables and future generic authoring/script field access.

## Reflection authoring model

Add a no-op macro header, for example:

```text
src/engine/scene/ComponentReflectionMacros.hpp
```

The macros exist for valid C++ and generator discovery; normal compilation should not need them to
perform work.

Target shape:

```cpp
enum class TestMode : uint8_t {
  Alpha,
  Beta,
};

struct TestExtensionComponent {
  float health{100.f};
  bool active{true};
  TestMode mode{TestMode::Alpha};
  AssetId attachment{};
};

TENG_REFLECT_COMPONENT_BEGIN(TestExtensionComponent, "teng.test.extension_proof")
  TENG_REFLECT_MODULE("teng.test", 1)
  TENG_REFLECT_SCHEMA_VERSION(1)
  TENG_REFLECT_STORAGE(Authored)
  TENG_REFLECT_VISIBILITY(Editable)
  TENG_REFLECT_ADD_ON_CREATE(false)
  TENG_REFLECT_FIELD(health, F32, DefaultF32(100.f), ScriptReadWrite)
  TENG_REFLECT_FIELD(active, Bool, DefaultBool(true), ScriptReadWrite)
  TENG_REFLECT_ENUM_FIELD(mode, "kind", "teng.test.extension_proof_kind",
                          DefaultEnum(TestMode::Alpha, "alpha"),
                          ScriptReadWrite,
                          TENG_ENUM_VALUE(TestMode::Alpha, "alpha", 0),
                          TENG_ENUM_VALUE(TestMode::Beta, "beta", 1))
  TENG_REFLECT_ASSET_FIELD(attachment, "attachment", "texture",
                           DefaultAssetId(""), ScriptReadWrite)
TENG_REFLECT_COMPONENT_END()
```

Rules:

- Scalar/vector field member names are canonical JSON keys unless an explicit key override macro is
  used.
- Enum fields must support `enum class` storage. JSON and future script exposure use canonical string
  keys, not raw C++ enumerator names or integer values.
- Enum reflection must record:
  - C++ enum type and enumerators.
  - Canonical authored string key per enumerator.
  - Stable numeric value per enumerator for diagnostics/cooked metadata if needed.
  - Default as both typed enumerator and authored string key.
- Supported first-slice field kinds match `ComponentFieldKind`: `Bool`, `I32`, `U32`, `F32`,
  `String`, `Vec2`, `Vec3`, `Vec4`, `Quat`, `Mat4`, `AssetId`, `Enum`.
- Defaults are required for every reflected field.
- Storage policies, visibility, add-on-create, module version, and schema version are required for
  every component.
- Script exposure values are `ScriptNone`, `ScriptRead`, and `ScriptReadWrite`; default to
  `ScriptNone` only when explicitly omitted by a macro variant.
- `EntityGuidComponent` and `Name` remain special identity/document metadata and are not authored
  scene components.

## Generated runtime surface

Add a generated-friendly reflection metadata layer, for example:

```text
src/engine/scene/ComponentRuntimeReflection.hpp
src/engine/scene/ComponentRuntimeReflection.cpp
```

Minimum records:

- Component key, module ID/version, schema version, storage policy, visibility, add-on-create.
- Field key, C++ member name, field kind, default value, asset metadata, enum metadata, script
  exposure.
- Type-erased generated thunks:
  - `register_flecs_fn(flecs::world&)`
  - `has_component_fn(flecs::entity)`
  - `serialize_component_fn(flecs::entity) -> nlohmann::json`
  - `deserialize_component_fn(flecs::entity, const nlohmann::json&)`
  - `apply_on_create_fn(flecs::entity)`
  - future-ready field get/set thunks taking a component pointer and JSON or a compact field value

Generated registration functions should adapt these records into existing builders:

- `ComponentRegistryBuilder`
- `FlecsComponentContextBuilder`
- `SceneSerializationContextBuilder`

Tighten `SceneSerializationContextBuilder::freeze()` so every registered `Authored` component must
have a serialization binding. Missing generated bindings should fail at context freeze, not during a
scene load.

## Generated JSON behavior

Generated JSON must preserve current canonical scene semantics:

- Serialize authored component fields in schema declaration order.
- Emit complete payloads, including default-valued fields.
- Deserialize only after validation has accepted field shape and schema compatibility.
- Use `AssetId::to_string()` and `AssetId::parse(...).value()`.
- Use enum authored string keys for JSON and convert to/from `enum class` storage.
- Do not serialize `RuntimeDerived`, `RuntimeSession`, `EditorOnly`, hidden, or identity-only state
  into scene component payloads unless a later storage policy explicitly allows it.

## Generated enum-class support

Enum support is required in this slice, not deferred.

Generated enum helpers should include:

- `to_key(EnumType value) -> std::string_view`
- `from_key(std::string_view key) -> EnumType`
- `to_stable_value(EnumType value) -> int64_t`
- schema emission through `ComponentEnumRegistration`

Failure behavior:

- The generator fails if an enum field has no enum values.
- The generator fails if enum authored keys are duplicated.
- The generator fails if enum stable values are duplicated.
- The generator fails if the default enum key is not one of the declared values.
- Runtime `from_key` may assert on unknown values because scene validation is the boundary.

Cooked v2 stance:

- Slice 7 may encode enum fields as string-table keys or stable enum numeric values.
- The reflection metadata must provide both so Slice 7 does not need to inspect C++ types or add a
  central enum table.

## Build integration

Add:

```text
scripts/teng_reflect_codegen.py
```

The generator accepts:

- output directory
- generated module name
- ordered list of headers to scan

Generated outputs:

- core generated header/source for engine scene components
- test generated header/source for the Slice 6 extension component
- optional build-dir JSON manifest for debugging codegen output

CMake changes:

- Find Python3 if not already available in the active CMake scope.
- Add `add_custom_command` outputs for generated files.
- Add generated core `.cpp` to `TENG_SCENE_SOURCES`.
- Add generated test `.cpp` to `teng_engine_smoke` or the test support target that owns the test
  extension component.
- Add `${CMAKE_BINARY_DIR}/generated` include directories only to targets that compile generated
  headers/sources.

Generator diagnostics should fail the configure/build step on:

- malformed reflection blocks
- duplicate component keys
- duplicate field keys
- missing module/schema/storage/visibility/default metadata
- unknown field kind
- unsupported C++ field type for the declared field kind
- malformed enum declarations
- unknown script exposure value

## Migration plan

### Task 1: Add plan and no-op macro surface DONE

- Add `ComponentReflectionMacros.hpp`.
- Define all reflection macros as no-op tokens that preserve valid C++.
- Add comments documenting that generator semantics, not macro runtime expansion, are authoritative.

### Task 2: Add generator fixture path

- Add `scripts/teng_reflect_codegen.py`.
- Add small fixture headers under tests or generator fixtures.
- Generate build-dir outputs from fixture headers.
- Cover scalar fields, asset fields, enum-class fields, runtime-only storage, and hidden/script
  exposure metadata.

### Task 3: Add runtime reflection records

- Add runtime metadata structs and thunk typedefs.
- Add helpers that adapt generated records into existing registry/Flecs/serialization builders.
- Keep this GPU-free and inside the scene library boundary.

### Task 4: Convert Slice 6 test extension first

- Change `TestExtensionComponent` from string enum storage to an `enum class`.
- Add reflection block for the test component.
- Generate its schema, Flecs binding, and JSON binding.
- Remove handwritten test extension registration bodies or reduce them to generated delegates.
- Keep tests proving extension without core serialization edits.

### Task 5: Convert core components

- Add reflection blocks to `SceneComponents.hpp` for:
  - `Transform`
  - `Camera`
  - `DirectionalLight`
  - `MeshRenderable`
  - `SpriteRenderable`
  - `LocalToWorld`
  - `FpsCameraController`
  - `EngineInputSnapshot`
- Keep `EntityGuidComponent` and `Name` unreflected.
- Replace core schema/Flecs/JSON handwritten tables with generated delegate calls.

### Task 6: Tighten registration invariants

- Make context freeze fail for authored components without serialization bindings.
- Keep Flecs context freeze requiring non-editor-only components to have Flecs bindings.
- Add diagnostics or assertions consistent with existing registry/builder style.

### Task 7: Update docs and Slice 7 dependency

- Update `component_schema_authoring_implementation_plan.md` to record Slice 6.5.
- Update `phase9_slice7_cooked_scene_v2_plan.md` so cooked v2 consumes generated reflection/schema
  field records and enum helpers.
- Add a short scripting note that Luau is the planned first VM, with reflection metadata as the future
  binding surface and VM integration out of scope.

### Task 8: Verification

- Run quick verification during iteration.
- Run full verification before completion.

## Tests

Add or update tests for:

- Generator accepts a fixture with scalar, asset, enum-class, runtime-only, and script exposure fields.
- Generator rejects duplicate component keys.
- Generator rejects duplicate field keys.
- Generator rejects missing defaults.
- Generator rejects enum fields with duplicate keys or duplicate stable values.
- Generated schema for core components matches current field keys, kinds, defaults, storage, visibility,
  module versions, and schema versions.
- Generated JSON round-trip for core authored components matches existing canonical output.
- Generated JSON round-trip for test extension component stores enum class values as authored string
  keys.
- Loading a scene with an unknown enum key fails validation before generated `from_key` is called.
- Serialization context freeze fails when an authored component lacks a generated serialization binding.
- Runtime-only components remain rejected from canonical scene JSON.
- Script metadata enumeration includes exposure flags without linking a Luau target.

Validation command:

```bash
./scripts/agent_verify.sh
```

Useful iteration command:

```bash
./scripts/agent_verify.sh --quick
```

## Non-goals

- No Luau submodule, VM, scheduler, hot reload, debugger, or sandbox policy.
- No script components or script file references in scene JSON.
- No generated component structs.
- No generated Flecs systems.
- No editor inspector UI.
- No cooked v2 implementation in this slice.
- No plugin hot-load or unknown-component preservation.
- No tolerant scene import mode.

## Retirement criteria

This slice is complete when:

- Adding a reflected authored component outside core requires only the C++ struct/header reflection
  block and target registration with the generator.
- Core component schema, Flecs registration, and JSON serialization are generated, not handwritten
  central tables.
- The Slice 6 test extension uses generated bindings and stores at least one reflected `enum class`
  field.
- Missing generated bindings fail early.
- Slice 7 can encode JSON/cooked fields from generated schema/reflection metadata without adding a new
  component identity or enum table.

