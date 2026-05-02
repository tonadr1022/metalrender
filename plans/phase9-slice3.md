# Phase 9 Slice 3: Declarative Field Schema Completion

## Status and Scope

Planned. This slice completes the typed field-schema layer already started in
`ComponentRegistry` and `CoreComponentRegistrar`.

This slice is metadata-only. It must not switch Flecs registration, scene JSON,
cook/dump, runtime scene construction, demo generation, renderer extraction, or
asset existence validation to schema-driven behavior yet.

The current implementation is directionally right but incomplete:

- `src/core/ComponentRegistry.hpp` has moved toward `fields`, while
  `src/core/ComponentRegistry.cpp` and tests still reference removed
  `field_keys`.
- Field-level `default_on_create` mixes entity creation policy with C++ default
  value metadata.
- Some core component schema fields do not match the real structs in
  `src/engine/scene/SceneComponents.hpp`.
- Optional authored components such as camera, lights, mesh renderable, and
  sprite renderable are incorrectly marked `default_on_create`.
- Runtime-session components need schema exposure metadata so
  `FpsCameraController` can be debug-inspectable while `EngineInputSnapshot`
  remains hidden.

## Relevant Current Code

- `src/core/ComponentRegistry.hpp`
  - Owns `ComponentFieldKind`, `ComponentFieldRegistration`,
    `ComponentRegistration`, `FrozenComponentRecord`,
    `ComponentRegistryBuilder`, and freeze output.
- `src/core/ComponentRegistry.cpp`
  - Performs module/component validation, stable id assignment, deterministic
    ordering, and frozen registry construction.
  - Still contains stale `field_keys` references that must be retired.
- `src/engine/scene/CoreComponentRegistrar.cpp`
  - Registers `teng.core` components and currently supplies field metadata.
  - Must be corrected to match `SceneComponents.hpp` defaults and field names.
- `src/engine/scene/SceneComponents.hpp`
  - Source of truth for concrete core component C++ defaults in this slice.
- `src/engine/Input.hpp`
  - Source of truth for `EngineInputSnapshot`; unordered key sets must not
    become authored declarative fields in this slice.
- `tests/core/ComponentRegistryTests.cpp`
  - Pure `teng_core` registry/freeze tests.
- Scene-linked test target or file
  - Needed for `register_core_components` assertions because the registrar
    lives outside `teng_core`.

## Target Metadata Model

### Component Storage and Visibility

Keep `ComponentStoragePolicy`; it is not dead code. It describes lifecycle and
ownership:

- `Authored`: persisted scene data.
- `RuntimeDerived`: computed runtime data.
- `RuntimeSession`: live-only runtime/session data.
- `EditorOnly`: editor-only data.

Add component-level schema visibility metadata, stored on both
`ComponentRegistration` and `FrozenComponentRecord`:

```cpp
enum class ComponentSchemaVisibility : uint8_t {
  Editable,
  DebugInspectable,
  Hidden,
};
```

Visibility describes schema/tool exposure, not storage:

- `Editable`: normal authored/editor schema surface.
- `DebugInspectable`: visible to tooling/debug views, not authored/persisted.
- `Hidden`: registered metadata exists, but declarative fields should not be
  exposed to authoring/debug surfaces by default.

Do not collapse visibility into storage. `FpsCameraController` and
`EngineInputSnapshot` are both `RuntimeSession`, but the former should be
`DebugInspectable` and the latter should be `Hidden`.

### Field Kinds

Update `ComponentFieldKind` to include unsigned and enum support while keeping
the existing width-specific style:

```cpp
enum class ComponentFieldKind : uint32_t {
  Bool,
  I32,
  U32,
  F32,
  String,
  Vec2,
  Vec3,
  Vec4,
  Quat,
  Mat4,
  AssetId,
  Enum,
};
```

Do not add `I64`, `U64`, or `F64` until a real component field needs them.
Existing core sorting fields remain `I32` because the C++ struct uses `int`.

### Authored Required Metadata

Rename field `required` to `authored_required`.

`authored_required` is declarative authoring/schema metadata only. It does not
change JSON, cook, loader behavior, or runtime invariants in this slice. It
means that when a component is represented as authored data, the field is part
of the required authored shape unless a later serialization slice explicitly
uses default metadata to synthesize missing values.

For core concrete component fields listed in this slice, set
`authored_required = true`.

### Default Values

Replace field-level `default_on_create` with optional default-value metadata:

```cpp
std::optional<ComponentFieldDefaultValue> default_value;
```

Defaults represent C++ default construction values, not entity creation policy.
Defaults are metadata-only in this slice; no JSON, cook, scene load, or runtime
construction path consumes them yet.

Default values are optional at the API level so future plugin, derived, generic,
or intentionally unspecified schemas can omit them. However,
`register_core_components` must provide complete defaults for all concrete core
fields it exposes.

Keep GLM out of `teng_core` schema metadata. Use compact core-owned value
structs plus `std::variant`; registry defaults are cold metadata, not a hot
frame path.

Recommended shape:

```cpp
struct ComponentDefaultVec2 {
  float x{}, y{};
};

struct ComponentDefaultVec3 {
  float x{}, y{}, z{};
};

struct ComponentDefaultVec4 {
  float x{}, y{}, z{}, w{};
};

struct ComponentDefaultQuat {
  float w{1.f}, x{}, y{}, z{};
};

struct ComponentDefaultMat4 {
  std::array<float, 16> elements{};
};

struct ComponentDefaultAssetId {
  std::string value;
};

struct ComponentDefaultEnum {
  std::string key;
};

using ComponentFieldDefaultValue = std::variant<
    bool,
    int64_t,
    uint64_t,
    float,
    std::string,
    ComponentDefaultVec2,
    ComponentDefaultVec3,
    ComponentDefaultVec4,
    ComponentDefaultQuat,
    ComponentDefaultMat4,
    ComponentDefaultAssetId,
    ComponentDefaultEnum>;
```

`ComponentDefaultMat4::elements` are column-major to match GLM memory layout.
Asset and enum defaults must use wrapper structs, not raw strings, so string
fields, asset IDs, and enum keys remain semantically distinct.

### Asset Field Metadata

Use a wrapper type for asset field metadata:

```cpp
struct ComponentAssetFieldMetadata {
  std::string expected_kind;
};
```

Attach it to fields as optional metadata, for example:

```cpp
std::optional<ComponentAssetFieldMetadata> asset;
```

Rules for this slice:

- Asset metadata is allowed only on `ComponentFieldKind::AssetId`.
- `AssetId` fields may omit metadata if they are truly generic.
- If present, `expected_kind` must be non-empty and syntactically valid.
- Use a conservative lowercase identifier format for expected kinds such as
  `model`, `texture`, and later `material`.
- Do not check project asset existence.
- Do not require or introduce a global asset-kind registry in this slice.

### Enum Metadata

Add real minimal enum descriptors rather than treating enum metadata as a tag on
integer fields:

```cpp
struct ComponentEnumValueRegistration {
  std::string key;
  int64_t value{};
};

struct ComponentEnumRegistration {
  std::string enum_key;
  std::vector<ComponentEnumValueRegistration> values;
};
```

Attach enum metadata to fields as optional metadata. Enum fields use
`ComponentFieldKind::Enum`.

Validation should reject:

- enum metadata on non-`Enum` fields.
- `Enum` fields without enum metadata.
- empty enum keys.
- empty enum value keys.
- duplicate enum value keys.
- duplicate numeric values, unless a future plan explicitly permits aliases.

### Component-Local Schema Validation Hook

Add an optional schema validation hook on `ComponentRegistration` and
`FrozenComponentRecord`.

Use a core-only function pointer signature:

```cpp
using ComponentSchemaValidationHook =
    void (*)(const FrozenComponentRecord& component, DiagnosticReport& report);
```

The hook validates frozen schema metadata only. It must not depend on Flecs,
JSON, renderer, scene runtime, asset registry, editor UI, or component instance
data. Slice 3 only proves hooks can be stored and invoked in tests; parsing and
serialization migration waits for a later slice.

## Registry Freeze Behavior

Finish the registry API migration from `field_keys` to `fields`:

- Remove stale `field_keys` use from freeze and tests.
- Validate duplicate fields by inspecting `ComponentFieldRegistration::key`.
- Copy `fields`, `visibility`, default values, asset metadata, enum metadata,
  and validation hooks into `FrozenComponentRecord`.
- Preserve deterministic component ordering and stable component id assignment.

Accumulate independent schema errors where practical. Field metadata validation
should still run for a component even if another component fails validation, and
local field metadata should still be checked when a component also references an
unknown or invalid module.

Diagnostics:

- Duplicate field keys: `schema.duplicate_field_key`.
- Empty field key: `schema.invalid_field_key`.
- Invalid asset metadata: `schema.invalid_asset_field_metadata`.
- Invalid enum metadata: `schema.invalid_enum_metadata`.
- Preserve existing module/component/storage diagnostics.

Default-value preservation is required, but this slice does not need full
cross-type validation for every possible default/kind pairing unless the
implementation can do it cleanly without expanding scope. The mandatory
validation additions are field key, asset metadata, and enum metadata.

## Core Component Registrar Requirements

`register_core_components` must register module `teng.core` version `1` and
correct the core component schemas below.

### `teng.core.transform`

- Storage: `Authored`.
- Visibility: `Editable`.
- `default_on_create = true`.
- Fields:
  - `translation`: `Vec3`, default `{0, 0, 0}`.
  - `rotation`: `Quat`, default identity `{w=1, x=0, y=0, z=0}`.
  - `scale`: `Vec3`, default `{1, 1, 1}`.

### `teng.core.camera`

- Storage: `Authored`.
- Visibility: `Editable`.
- `default_on_create = false`.
- Fields:
  - `fov_y`: `F32`, default `1.04719755`.
  - `z_near`: `F32`, default `0.1`.
  - `z_far`: `F32`, default `10000`.
  - `primary`: `Bool`, default `false`.

### `teng.core.directional_light`

- Storage: `Authored`.
- Visibility: `Editable`.
- `default_on_create = false`.
- Fields:
  - `direction`: `Vec3`, default `{0.35, 1, 0.4}`.
  - `color`: `Vec3`, default `{1, 1, 1}`.
  - `intensity`: `F32`, default `1`.

### `teng.core.mesh_renderable`

- Storage: `Authored`.
- Visibility: `Editable`.
- `default_on_create = false`.
- Fields:
  - `model`: `AssetId`, default empty asset id, expected asset kind `model`.

### `teng.core.sprite_renderable`

- Storage: `Authored`.
- Visibility: `Editable`.
- `default_on_create = false`.
- Fields:
  - `texture`: `AssetId`, default empty asset id, expected asset kind `texture`.
  - `tint`: `Vec4`, default `{1, 1, 1, 1}`.
  - `sorting_layer`: `I32`, default `0`.
  - `sorting_order`: `I32`, default `0`.

### `teng.core.local_to_world`

- Storage: `RuntimeDerived`.
- Visibility: `DebugInspectable`.
- `default_on_create = false`.
- Fields:
  - `value`: `Mat4`, default identity matrix, column-major.

### `teng.core.fps_camera_controller`

- Storage: `RuntimeSession`.
- Visibility: `DebugInspectable`.
- `default_on_create = false`.
- Fields from `FpsCameraController`:
  - `pitch`: `F32`, default `0`.
  - `yaw`: `F32`, default `0`.
  - `max_velocity`: `F32`, default `5`.
  - `move_speed`: `F32`, default `10`.
  - `mouse_sensitivity`: `F32`, default `0.1`.
  - `look_pitch_sign`: `F32`, default `1`.
  - `mouse_captured`: `Bool`, default `false`.

### `teng.core.engine_input_snapshot`

- Storage: `RuntimeSession`.
- Visibility: `Hidden`.
- `default_on_create = false`.
- Fields: none in this slice.

Do not expose unordered key sets as declarative authored fields. Do not add
partial debug fields for `EngineInputSnapshot` unless a later slice designs that
debug surface explicitly.

## Files and Targets

Expected implementation files:

- Change `src/core/ComponentRegistry.hpp`.
- Change `src/core/ComponentRegistry.cpp`.
- Change `src/engine/scene/CoreComponentRegistrar.cpp`.
- Update `tests/core/ComponentRegistryTests.cpp`.
- Add a focused scene-linked schema test file, likely
  `tests/scene/CoreComponentSchemaTests.cpp`, or place equivalent tests in an
  existing scene-linked target if that is the least disruptive CMake path.
- Update CMake only as needed to compile the new scene-linked schema test.

No implementation code should be added to scene serialization, scene cook,
runtime `Scene`, `SceneManager`, renderer, asset service, app startup, or demo
generation for this slice.

## Tests

### Pure Registry Tests

Update `tests/core/ComponentRegistryTests.cpp`:

- Existing tests compile against `.fields`.
- Duplicate field key test uses typed descriptors.
- Add field-order preservation test on a frozen component.
- Add default-value preservation test covering at least:
  - bool
  - float
  - vec3
  - quat
  - AssetId
  - enum metadata and enum default key
- Add invalid field metadata tests for:
  - empty field key
  - invalid asset expected-kind metadata
  - asset metadata on non-asset field
  - missing enum metadata on enum field
  - enum metadata on non-enum field
  - empty enum key/value key
  - duplicate enum keys
  - duplicate enum numeric values
- Add hook storage/invocation test that appends a stable diagnostic.
- Add visibility preservation test.
- Keep existing module/component/storage diagnostics covered.

### Scene-Linked Schema Test

Add a narrow scene-linked schema test, not a runtime smoke test.

The test should:

- Build a `ComponentRegistryBuilder`.
- Call `register_core_components`.
- Freeze the registry.
- Assert core component keys and deterministic lookup.
- Assert storage policies.
- Assert schema visibility.
- Assert component `default_on_create` policy.
- Assert field names, kinds, `authored_required`, defaults, stable IDs, and
  asset kind metadata.
- Assert camera, directional light, mesh renderable, and sprite renderable are
  not default-on-create.
- Assert `EngineInputSnapshot` is hidden and has no fields.

The test must not launch `metalrender`, tick runtime scenes, validate Flecs
component registration, load JSON, cook scenes, or touch renderer behavior.

## Migration Scaffolding and Retirement Criteria

Temporary scaffolding:

- Default-value metadata is stored but not consumed.
- `authored_required` is stored but not consumed by JSON/cook.
- Component validation hooks are stored and callable in tests only.
- Core schemas exist alongside legacy central serialization/cook code.

Retirement criteria:

- Later Phase 9 slices switch Flecs registration, JSON, cook/dump, validation,
  and scene authoring to the frozen registry.
- Central hand-maintained component serialization/cooked identity is removed per
  the Phase 9 implementation plan.
- Default metadata and `authored_required` become part of schema-driven
  authoring/serialization rules, or are removed if the later design makes them
  redundant.
- Schema validation hooks either become part of the real validation path or are
  removed if unused by the end of the schema migration.

## Risks and Tradeoffs

- `authored_required` can become misleading if later slices do not define how it
  interacts with default synthesis. The rename is intended to keep its scope
  explicit until serialization consumes it.
- `std::variant` default values add some API verbosity, but the metadata is
  cold and strongly typed. This is preferable to stringly typed defaults.
- Keeping GLM out of `teng_core` avoids leaking engine math dependencies into
  core diagnostics/schema tooling, at the cost of small neutral value structs.
- Visibility and storage overlap in common cases, but separating them avoids
  abusing storage to express debug/editor exposure policy.
- Enum aliases are rejected for now. If aliases become a real need, that should
  be an explicit schema feature, not an accidental permissive behavior.
- Asset expected-kind validation is syntactic only. Project asset existence and
  kind checking remain later project validation work.

## Validation Strategy

Narrow while implementing:

```bash
cmake --build --preset Debug --target teng_core_tests engine_scene_smoke
./build/Debug/bin/teng_core_tests
./build/Debug/bin/engine_scene_smoke
```

Final required verification:

```bash
./scripts/agent_verify.sh
```

Use `./scripts/agent_verify.sh --format` if implementation changes first-party
C/C++ formatting.

## Explicit Non-Goals

- No schema-driven Flecs registration.
- No JSON load/save behavior changes.
- No cooked scene format changes.
- No scene authoring API changes.
- No runtime scene construction changes.
- No demo generation changes.
- No asset existence/type validation against a project registry.
- No renderer, render-scene, app startup, or smoke behavior changes.
- No editor UI or inspector behavior.

## Open Questions

None for Slice 3 after the design review. Later slices still need to define how
`authored_required`, defaults, schema visibility, asset metadata, and validation
hooks are consumed by JSON, cook, tools, and editor authoring.
