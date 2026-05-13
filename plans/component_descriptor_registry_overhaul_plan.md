# Component descriptor registry overhaul plan

**Status:** proposed architecture slice.

**Related plans:** [`component_schema_authoring_implementation_plan.md`](component_schema_authoring_implementation_plan.md),
[`phase9_slice6_5_component_reflection_codegen_plan.md`](phase9_slice6_5_component_reflection_codegen_plan.md),
[`phase9_slice7_cooked_scene_v2_plan.md`](phase9_slice7_cooked_scene_v2_plan.md).

## Summary

Component reflection codegen is now the practical source of component truth. The runtime still treats
that generated data as three separate registration streams: schema records, Flecs thunks, and JSON
serialization thunks. This slice should collapse those streams into one generated descriptor model.

The registry becomes the hub: it freezes static generated component modules, validates all component
schema and operation invariants, derives stable ids and module indexes, and exposes immutable
component records. Flecs component context and scene serialization context become registry-derived
views instead of independently populated builders.

Static linked generated modules are the only extension model for this slice. Runtime plugin or
hot-load component registration remains out of scope.

## Current state

The current codegen path emits:

- `scene::ReflectedComponentRecord` arrays for schema.
- `teng::engine::ReflectedFlecsThunks` arrays for Flecs registration and add-on-create.
- `teng::engine::ReflectedSerializationThunks` arrays for authored JSON.

Those are adapted through:

- `ComponentRuntimeReflection.*`
- `ComponentRegistryBuilder`
- `FlecsComponentContextBuilder`
- `SceneSerializationContextBuilder`

This was a good migration bridge, but it now duplicates component identity and lets generated facts
fall through cracks. For example, `member_name` and `script_exposure` exist in
`ReflectedFieldRecord`, but are not retained in `ComponentFieldRegistration`.

## Target API shape

Add a descriptor surface owned by the scene/component reflection layer:

```cpp
namespace teng::engine::scene {

enum class ScriptExposure : uint8_t {
  None,
  Read,
  ReadWrite,
};

struct ComponentFieldDescriptor {
  std::string_view key;
  std::string_view member_name;
  ComponentFieldKind kind{};
  bool authored_required{true};
  ComponentFieldDefaultValue default_value;
  std::optional<ComponentAssetFieldMetadata> asset;
  std::optional<ComponentEnumRegistration> enumeration;
  ScriptExposure script_exposure{ScriptExposure::None};
};

struct ComponentTypeOps {
  RegisterFlecsFn register_flecs_fn{};
  ApplyOnCreateFn apply_on_create_fn{};
  HasComponentFn has_component_fn{};
  SerializeComponentFn serialize_fn{};
  DeserializeComponentFn deserialize_fn{};
};

struct ComponentDescriptor {
  std::string_view component_key;
  uint32_t schema_version{1};
  ComponentStoragePolicy storage{ComponentStoragePolicy::Authored};
  ComponentSchemaVisibility visibility{ComponentSchemaVisibility::Editable};
  bool add_on_create{};
  ComponentSchemaValidationHook schema_validation_hook{nullptr};
  std::span<const ComponentFieldDescriptor> fields;
  ComponentTypeOps ops;
};

struct ComponentModuleDescriptor {
  std::string_view module_id;
  uint32_t module_version{1};
  std::span<const ComponentDescriptor> components;
};

}  // namespace teng::engine::scene
```

The exact header placement can differ, but the public concept should stay this compact: generated
module descriptors contain both schema and typed operations.

## Registry changes

Replace `ComponentRegistryBuilder`, `ComponentRegistration`, and `ComponentFieldRegistration` as
public construction APIs with registry freeze from static generated module descriptors.

Recommended freeze API:

```cpp
namespace teng::engine::scene {

bool try_freeze_component_registry(std::span<const ComponentModuleDescriptor> modules,
                                   ComponentRegistry& out,
                                   core::DiagnosticReport& report);

}  // namespace teng::engine::scene
```

If call sites need to compose several independently generated libraries, pass a flat span/vector of
module descriptors. The implementation should not require registering modules and components in
separate calls.

`FrozenComponentRecord` should retain all field facts needed by future editor/script/cooked work:

- component key, module id/version, schema version, storage, visibility, stable id
- field key, member name, kind, default, asset metadata, enum metadata, script exposure
- component ops

The frozen registry may copy strings and field vectors for simplicity, or hold views into static
generated descriptors. If it stores views, document that generated descriptor storage must outlive
the registry. Since descriptors are linked static data, view storage is acceptable.

Registry freeze validates:

- duplicate module id with same version is an error
- duplicate module id with different versions is an error
- duplicate component keys are an error
- stable component id collisions are an error
- component module membership is derived from descriptor nesting, not repeated per component
- field keys are non-empty and unique per component
- asset metadata is present only on `AssetId` fields and has a valid expected kind
- enum fields have enum metadata, non-empty enum keys, non-empty value lists, unique value keys, and
  unique numeric values
- non-enum fields do not carry enum metadata
- non-`EditorOnly` components have `register_flecs_fn`
- `add_on_create` components have `apply_on_create_fn`
- `Authored` components have `has_component_fn`, `serialize_fn`, and `deserialize_fn`
- schema validation hooks still run after built-in validation

Retire `ComponentRegistryBuilder::find`; it currently implies sorted lookup over an unsorted builder
vector and should not survive the overhaul.

## Codegen changes

Update `teng-component-codegen` so each generated module emits one descriptor surface:

- static field descriptor arrays
- static component descriptor arrays with schema and ops together
- one static module descriptor or module descriptor array
- one exported function returning module descriptors, for example:

```cpp
std::span<const scene::ComponentModuleDescriptor> core_component_modules();
```

Retire generated functions:

- `register_*_reflected_components`
- `register_*_reflected_flecs`
- `register_*_reflected_serialization`

Retire runtime adapters in `ComponentRuntimeReflection.*` once all generated targets expose module
descriptors directly.

Keep JSON serialization/deserialization thunks generated for authored components only. For
non-authored components, leave JSON ops null and rely on registry validation policy.

The generator should keep sorting components by component key for deterministic output. Within a
component, preserve C++ declaration field order because JSON canonicalization and future cooked
encoding depend on schema order.

## Context changes

Replace `FlecsComponentContextBuilder` with a registry-derived construction function:

```cpp
bool make_flecs_component_context(const scene::ComponentRegistry& registry,
                                  FlecsComponentContext& out,
                                  core::DiagnosticReport& report);
```

This function iterates frozen registry components in registry order, skips `EditorOnly`, appends
`register_flecs_fn` for every remaining component, and appends `apply_on_create_fn` for components
with `add_on_create`.

Replace `SceneSerializationContextBuilder` with a registry-derived construction function:

```cpp
SceneSerializationContext make_scene_serialization_context(const scene::ComponentRegistry& registry);
```

`SceneSerializationContext` should no longer own a separate binding list. It should reference the
registry and use authored component ops from frozen component records. Existing serialization logic
that sorts bindings should instead sort or iterate authored frozen component records.

Validation should determine whether an authored component has JSON support by looking at the
component's frozen ops, not by searching a separate binding array.

## Call-site migration

Change construction sites to gather generated module descriptors first, then freeze one registry.

Core-only shape:

```cpp
std::vector<scene::ComponentModuleDescriptor> modules;
append_component_modules(modules, core_component_modules());
scene::ComponentRegistry registry;
try_freeze_component_registry(modules, registry, report);
FlecsComponentContext flecs = make_flecs_component_context(registry, report);
SceneSerializationContext serialization = make_scene_serialization_context(registry);
```

Core plus extension shape:

```cpp
append_component_modules(modules, core_component_modules());
append_component_modules(modules, test_extension_component_modules());
```

Update the engine, `teng-scene-tool`, and smoke test helpers. The extension proof should still show
that adding a test component requires only linking the generated library and adding its module span to
the registry freeze input.

## CMake and generated target impact

Keep generated C++ under `${CMAKE_BINARY_DIR}/generated/...` as today.

Generated libraries should continue linking against `teng_scene`, but after this refactor they should
only need to expose descriptor arrays and typed thunk functions. They should not depend on separate
builder adapter APIs.

Core wrapper files can collapse to one exported module-descriptor accessor. Remove the separate core
schema/Flecs/serialization registration wrappers after all call sites migrate.

## Tests

Update or add focused tests for:

- registry freeze succeeds for valid static generated descriptors
- duplicate module id/version diagnostics
- duplicate component key diagnostics
- unknown old-builder-style module/component mismatch paths are removed rather than preserved
- missing Flecs ops on non-`EditorOnly` components
- missing apply-on-create ops when `add_on_create` is true
- missing JSON ops on `Authored` components
- `EditorOnly` components do not require Flecs or JSON ops
- field order is preserved
- defaults, asset metadata, enum metadata, member names, and script exposure survive into frozen
  records
- generated fixture descriptors adapt directly into registry/context/serialization paths
- test extension scene JSON round-trip still works without central serialization tables

Negative codegen tests should continue covering:

- duplicate component keys
- duplicate field keys
- duplicate enum keys
- missing required metadata

Add a codegen test for unannotated enum values if the intended policy becomes "all enum values in a
reflected enum field must be annotated." Recommended policy: make missing enum value annotations a
codegen error for reflected enum fields.

## Validation

Run:

```bash
./scripts/agent_verify.sh --quick
./scripts/agent_verify.sh
```

If only generator or generated component reflection changes are in flight, build/test failures in the
reflection fixture and scene serialization tests are the first things to inspect.

## Non-goals

- Do not implement cooked scene v2 in this slice.
- Do not implement generic editor/script typed field get/set thunks in this slice.
- Do not preserve old builder APIs as compatibility shims after the migration is complete.
- Do not introduce runtime plugin or hot-loaded component module support.
- Do not change scene JSON v2 wire shape except where required by existing registry/context API
  migration.

## Retirement criteria

This slice is complete when:

- `ComponentRegistryBuilder`, `ComponentRegistration`, `ReflectedComponentRecord`,
  `ReflectedFlecsThunks`, `ReflectedSerializationThunks`, `FlecsComponentContextBuilder`, and
  `SceneSerializationContextBuilder` are gone.
- Generated component code exposes module descriptors, not registration streams.
- Engine, scene tool, core tests, smoke tests, and test extension all construct component runtime
  state from `ComponentRegistry`.
- `./scripts/agent_verify.sh` passes.
