# Scene serialization v2 contract

**Status:** Contract target for Phase 9 after the component schema overhaul. This document is no
longer the source of truth for component registration. Component schemas, field metadata, authoring
transactions, storage policies, and registry lifecycle are defined by
[`component_schema_authoring_model.md`](component_schema_authoring_model.md).

**Parent roadmap:** [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md).

## Scope

This document defines canonical scene bytes derived from a frozen component registry:

- canonical JSON scene files
- cooked scene byte identity/layout rules
- validation/canonicalization expectations
- migration stance for future schema changes

Out of scope:

- player saves/progression
- editor undo stack
- asset bundle/release packaging
- plugin hot-load and unknown-component preservation
- old v1 runtime compatibility

## Canonical JSON v2

Files are UTF-8 JSON with convention `*.tscene.json`. Scene JSON uses `nlohmann/json` in the engine
code path.

Top-level shape:

```json
{
  "scene_format_version": 2,
  "schema": {
    "registry_fingerprint": "example",
    "required_modules": [
      { "id": "teng.core", "version": 1 }
    ],
    "components": {
      "teng.core.transform": 1,
      "teng.core.camera": 1
    }
  },
  "scene": {
    "name": "demo"
  },
  "entities": [
    {
      "guid": 10001,
      "name": "camera",
      "components": {
        "teng.core.transform": {
          "translation": [0, 0, 3],
          "rotation": [1, 0, 0, 0],
          "scale": [1, 1, 1]
        },
        "teng.core.camera": {
          "fov_y": 1.04719755,
          "z_near": 0.1,
          "z_far": 10000.0,
          "primary": true
        }
      }
    }
  ]
}
```

Exact field payloads are generated from the component schema registry.

## Envelope rules

- `scene_format_version` identifies the JSON envelope/layout.
- `schema.registry_fingerprint` is deterministic diagnostic/cache metadata, not an exact-load gate.
- `schema.required_modules` declares the modules required by this file.
- `schema.required_components` maps each component key used by the file to the current component schema version.
- `scene.name` is required.
- `entities` is required and sorted by unsigned `EntityGuid::value` ascending on canonical save.
- Each entity has `guid`, optional `name`, and `components`.
- `guid` remains an unsigned JSON number while `EntityGuid::value` stays within IEEE-754 safe integer
  range.
- Entity `name` is entity/document metadata mirrored into Flecs name state; it is not a component
  payload.
- `EntityGuidComponent` is identity infrastructure; it is not serialized inside `components`.

Runtime/editor load fails by default on unknown top-level keys, unknown envelope keys, unknown
component keys, unsupported module versions, unsupported component schema versions, malformed fields,
or noncanonical runtime-only component payloads.

Tooling may later add explicit tolerant modes, but Phase 9 does not preserve unknown components.

## Component keys and ordering

Scene JSON component keys are globally namespaced registry keys, e.g. `teng.core.transform`.

Canonical ordering:

- entities sorted by `EntityGuid`
- components within an entity sorted by namespaced component key
- fields within a component emitted in schema declaration order

Canonical payloads emit every required field. Missing-field-as-default and default elision are out of
scope for Phase 9.

## Authored vs runtime-only

Authored components are serialized if present. Runtime-only components are rejected in canonical scene
payloads unless a future schema version explicitly changes policy.

Current intended core classification:

| Component/state | Policy | Disk |
|-----------------|--------|------|
| `Transform` | `Authored`, default-on-create | yes |
| `Camera` | `Authored` | yes when present |
| `DirectionalLight` | `Authored` | yes when present |
| `MeshRenderable` | `Authored` | yes when present |
| `SpriteRenderable` | `Authored` | yes when present |
| `LocalToWorld` | `RuntimeDerived` | never |
| `FpsCameraController` | `RuntimeSession` / debug-inspectable | no |
| `EngineInputSnapshot` | hidden `RuntimeSession` | no |
| `EntityGuidComponent` | identity infrastructure | entity `guid`, not component |
| `Name` | document/entity metadata | entity `name`, not component |

`Transform` is parent-local. Actual hierarchy/parent relationship behavior is deferred.

## Validation and canonicalization

Validation uses structured diagnostics from the core diagnostics facility described in
[`component_schema_authoring_model.md`](component_schema_authoring_model.md).

Validation layers:

1. envelope and schema/module metadata
2. component key lookup in the frozen registry
3. field shape/type/constraint validation
4. typed component-local validation hooks
5. optional project validation when an asset registry/project context is available

Canonicalization is an explicit operation for tools and is used internally by save paths. Runtime/editor
in-memory scene serialization should be canonical by construction.

## Versioning and migration

Versioning is split:

- `scene_format_version`: JSON envelope/layout version.
- module versions: contributed component vocabulary compatibility.
- per-component schema versions in `schema.required_components`.
- cooked `binary_format_version`: cooked byte layout version.

The old single `registry_version` concept is retired.

Canonical scene files should be normalized to current supported schema versions before save completes.
Live runtime/editor scenes do not carry mixed old component versions.

Phase 9 does not need runtime v1 JSON compatibility. Existing demo scenes may be deleted and regenerated
through the new schema-aware C++ path. `teng-scene-tool migrate` should exist as the future migration
entry point, but it does not need a v1 preservation bridge unless a real authored-content preservation
need appears.

## Cooked scene v2

Cooked data is runtime-facing and tied to the active frozen schema set. It rejects unknown/newer
component schema versions.

Cooked files carry:

- cooked magic
- `binary_format_version`
- `scene_format_version`
- schema/module compatibility metadata sufficient for diagnostics and cache invalidation
- string/name tables as needed
- entity records
- component records keyed by stable component ID and component schema version
- field blobs encoded from schema declaration order

Cooked component identity:

- stable 64-bit ID derived from the namespaced component key
- registry freeze fails on hash collision
- no fixed central `ComponentBit` enum as the identity model

Cooked field encoding:

- automatic for supported schema field primitives
- field order follows schema declaration order
- no per-field IDs/keys inside canonical field blobs
- custom cooked codec hooks only for components that cannot use field-schema encoding

If local compact indexes or masks are useful inside one cooked file, they may be generated from the
file/registry contents. They must not be hand-assigned global enum bits.

JSON/cooked parity remains required: cook then dump should reproduce canonical JSON semantics for the
same schema version.

## Tooling

`teng-scene-tool` / `teng_scene_tool_lib` remain GPU-free and should validate, canonicalize, migrate,
cook, dump, and optionally generate demo scenes through the same schema serializer as editor save.

Handwritten Python construction of canonical scene JSON is retired. Python may orchestrate assets or
invoke C++ tools, but it must not be the scene component schema.

## Required tests

Phase 9 should include:

- schema freeze duplicate/collision/invalid-policy diagnostics
- canonical JSON save/load for core components
- strict unknown component rejection
- runtime-only component rejection
- complete default field emission
- deterministic entity/component/field ordering
- cook/dump JSON parity
- test-only component registered outside core serialization that round-trips JSON and cook/dump
- asset reference syntax validation separate from project asset existence validation

## Resolved decisions

| Topic | Decision |
|-------|----------|
| Canonical scene text | JSON v2, `*.tscene.json`. |
| JSON library | `nlohmann/json` for scene bytes. |
| Component keys | Namespaced schema keys, e.g. `teng.core.transform`. |
| Field keys | Stable authored keys, distinct from C++ member names. |
| Entity label | Entity-level `name`; not a normal component payload. |
| Stable identity | Entity-level `guid`; `EntityGuidComponent` is runtime mirror/infrastructure. |
| `LocalToWorld` | Runtime-derived, never on disk. |
| Defaults | Complete payloads emitted; no default elision in Phase 9. |
| Unknown components | Runtime/editor strict failure. |
| v1 compatibility | No runtime v1 support required; regenerate demo scenes. |
| Cooked identity | Stable hash of namespaced component key, not central bit enum. |
| Endianness | Little-endian; big-endian unsupported unless a future plan changes this. |
