# Component schema and authoring model

**Status:** Authoritative Phase 9 design target. This document supersedes component-registration
direction previously embedded in [`scene_serialization_design.md`](scene_serialization_design.md).

**Parent roadmap:** [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) —
Phase 9, **Component Schema and Authoring Model Overhaul**.

**Implementation sequencing:** [`component_schema_authoring_implementation_plan.md`](component_schema_authoring_implementation_plan.md).

## Purpose

Phase 9 replaces the current hand-maintained scene component serialization model with a
project/process-level component schema registry that is the single source of truth for authored
component data.

The registry must drive:

- Flecs component registration for scenes.
- Canonical JSON serialization, validation, deserialization, and migration.
- Cooked component identity and field encoding.
- Minimal editor/tool field metadata.
- Scene authoring mutations through an undo-ready transaction boundary.
- Game/module extension without editing engine central serialization tables.

This is an engine component model overhaul, not an editor foundation slice.

## Non-goals

- Full editor product target, hierarchy panel, inspector UX, play/stop, reload prompts, and undo stack
  implementation.
- Prefabs, variants, overrides, hierarchy/parenting behavior, asset browser UX, plugin hot-loading,
  scripting bindings, input rebinding, player saves, or shipping bundle polish.
- Runtime support for old v1 scene JSON. Existing generated demo scenes may be deleted and regenerated
  through the new v2 path.

## Core decisions

1. **Declarative C++ schema first.** Component schemas are declared in C++ in a form that future codegen
   can emit. Field metadata is data, not hidden in handwritten serializer functions.
2. **One source of truth.** A component schema defines stable keys, fields, defaults, validation,
   storage policy, schema version, cooked identity, and editor/tool semantics.
3. **Explicit startup registration.** Modules expose registrar functions; no static self-registration.
4. **Shared frozen registry.** A project/process constructs a registry, freezes it, then creates scenes,
   tools, runtime load paths, and editor documents from that immutable registry.
5. **Namespaced component keys.** Canonical component keys are globally namespaced, e.g.
   `teng.core.transform`. Scene JSON uses those same keys.
6. **Stable cooked identity.** Cooked component IDs are derived from stable namespaced keys with a stable
   64-bit hash. Registry freeze fails on collisions.
7. **Schema-driven serialization and cook.** Supported field primitives get automatic JSON and cooked
   encoding in declared field order. Custom hooks are escape hatches.
8. **Authoring mutations are observable.** Editor/tool mutations go through a document/transaction API
   that can mark dirty now and produce undo commands later.

## Registry lifecycle

Registration is explicit and ordered by startup code:

```cpp
ComponentRegistryBuilder components;
register_core_components(components);
register_game_components(components);

Result<ComponentRegistry> registry = components.freeze();
```

Generated code should be able to emit the same registrar functions later.

`ComponentRegistryBuilder::freeze()` returns an immutable `ComponentRegistry`. Scenes and scene managers
require a frozen registry/context; there is no implicit default core registry in production paths.

Freeze validation must reject at least:

- duplicate module IDs with incompatible versions
- duplicate component keys
- duplicate cooked component IDs / hash collisions
- duplicate field keys within a component
- unsupported field types for automatic JSON/cook
- authored components without field schemas unless they declare explicit custom codecs
- missing Flecs type registration for a schema entry
- invalid storage/default policy combinations
- non-deterministic schema ordering where canonical output would depend on registration order

Schema validation and scene validation may share diagnostic primitives and constraint evaluators, but
they validate different objects. Freezing validates the schema graph; scene validation validates data
against a frozen schema.

## Modules

Each registered component records the module that contributed it. Module identity is explicit rather
than inferred only from the component key.

Example module IDs:

- `teng.core`
- `game.platformer`
- `tools.import`

Scene files declare required modules and component schema versions in their envelope; see
[`scene_serialization_design.md`](scene_serialization_design.md).

## Component storage policies

Every engine-known component should be classified, even if it is not authored:

| Policy | Meaning |
|--------|---------|
| `Authored` | Saved in canonical scenes, eligible for cook, editable through authoring tools. |
| `RuntimeDerived` | Never authored; derived from other state, visible at most as read-only/debug metadata. |
| `RuntimeSession` | Session/input/cache/controller state; not saved, usually hidden or debug-only. |
| `EditorOnly` | Editor document/UI state; not runtime scene data. |

`LocalToWorld` is `RuntimeDerived`. `EngineInputSnapshot` is hidden `RuntimeSession`.
`FpsCameraController` may be `RuntimeSession` with field metadata for debug/editor tuning, but it is
not saved unless a future authored controller component is deliberately introduced.

`EntityGuidComponent` and entity `Name` are scene identity/document metadata mirrored into Flecs, not
normal authored components in the `components` map.

## Field model

Each authored component field has a stable authored key distinct from the C++ member name:

```cpp
schema.component<Transform>("teng.core.transform")
    .module("teng.core")
    .version(1)
    .storage(ComponentStorage::Authored)
    .default_on_create(true)
    .field("translation", &Transform::translation)
    .field("rotation", &Transform::rotation)
    .field("scale", &Transform::scale);
```

The exact C++ API may differ, but the resulting schema must contain:

- component key
- module ID and module version
- component schema version
- storage policy and visibility policy
- stable cooked ID
- declared field order
- field keys, field kinds, constraints, and member accessors
- default construction/default-value policy
- optional component-local validation hook
- optional migration hooks for older payload versions

Phase 9 field primitives should include:

- bool
- signed/unsigned integers
- float
- string
- vec2/vec3/vec4
- quaternion
- `AssetId` with declared expected asset kind
- minimal enum metadata with stable authored enum value keys

Arrays, maps, variants, nullable fields, and flags can wait for concrete use cases.

Fields are required in canonical payloads. Canonical save emits complete payloads; default elision is
out of scope. Defaults come from C++ default construction unless explicitly overridden by schema.

Field order defines canonical JSON field emission order and cooked binary field order.

## Validation

Validation layers:

1. Scene envelope/version/module checks.
2. Component key lookup in the frozen registry.
3. Field shape/type/constraint validation.
4. Component-local validation on typed `const T&`.
5. Later project validation for asset existence/type, reference closure, and cross-entity rules.

Component-local validation receives typed current component data, not raw JSON. Migration hooks transform
older raw JSON payloads to current canonical payload JSON before typed parsing.

Asset reference fields validate `AssetId` syntax in schema/basic scene validation. Existence, tombstone,
asset kind, dependency closure, and cookability are project validation concerns when an `AssetRegistry`
is available.

## Diagnostics

Phase 9 introduces core structured diagnostics without forcing every engine `Result<T>` call site to
migrate immediately.

Diagnostics should include:

- stable string code, e.g. `schema.duplicate_field`, `scene.unknown_component`
- severity (`error`, `warning`, possibly `info`)
- typed path segments with string rendering at CLI/editor boundaries
- human-readable message

Registry freeze and scene validation should collect multiple diagnostics where practical. IO, corrupted
binary reads, and impossible internal invariants may still fail fast.

## Scene and Flecs integration

`Scene` and `SceneManager` are created with a frozen scene/component context. A manager uses one registry
for every scene it creates.

The registry drives Flecs component type registration. `Scene::register_components()` must not remain a
hand-maintained list of engine component types.

The registry also carries default-on-create policy:

- `Transform` is authored and default-added to normal new entities.
- `LocalToWorld` is runtime-derived and default-added where systems require it.
- Optional components such as `Camera`, `DirectionalLight`, `MeshRenderable`, `SpriteRenderable`, and
  game components are absent until added.

The schema system must support true component absence even when some components are default-on-create.

System registration is a parallel startup composition concern, not part of `ComponentRegistry`.
Long-term module shape should allow:

```cpp
register_core_components(component_registry_builder);
register_core_systems(system_registry_builder);
```

Tools that only validate/cook scenes must not link runtime systems unnecessarily.

## Authoring layer

Runtime `Scene` remains lean. Authoring/editor/tools use a separate GPU-free authoring library, e.g.
`teng_scene_authoring`, containing:

- `SceneDocument` or equivalent
- scene-builder API
- schema-aware component and field mutation
- transaction boundaries
- dirty tracking
- future undo-command emission points
- demo scene generation helpers

`SceneDocument` owns or strongly binds to the authoritative edit scene and carries document metadata
such as path and dirty state. It should not be folded into runtime `SceneManager`.

Authoring APIs should support both typed and schema-key driven operations:

```cpp
builder.add_component<Transform>(entity, Transform{});
builder.set_field(entity, "teng.core.transform", "translation", Vec3{});
```

Typed operations still resolve through the frozen registry. If a type/key is not registered, the
operation fails.

Editor-style field edits should use draft/transaction semantics:

1. copy current component state
2. mutate draft fields
3. validate field and component constraints
4. commit or reject

Phase 9 needs dirty tracking and operation boundaries, not a full undo stack. The operation boundary
must preserve enough information for future undo/redo: operation kind, entity ID, component key, field
key when applicable, and before/after payloads.

## Serialization and cook

Canonical scene JSON and cooked scene data are derived from the frozen registry. The current central
`ComponentCodec` table and fixed cooked component bit enum/mask model are retirement targets.

See [`scene_serialization_design.md`](scene_serialization_design.md) for v2 file and cooked byte
contracts.

## Demo generation

Handwritten Python construction of canonical scene component JSON must be removed. Python may still
orchestrate non-scene assets or invoke tools, but canonical scene bytes must come from C++ schema-aware
authoring/generation code and the same serializer used by editor save.

Demo generation may be a narrow `teng-scene-tool` subcommand or a separate executable, but it should
reuse the GPU-free scene schema/authoring libraries.

## Phase 9 exit criteria

Phase 9 exits when:

- Core engine components are registered through declarative C++ schemas.
- A frozen process/project-level registry drives Flecs component registration.
- Runtime scene creation requires an explicit frozen registry/context.
- Canonical JSON v2 uses namespaced component keys and schema/module metadata.
- JSON load/save is schema-driven and emits complete canonical payloads.
- Cook/dump uses schema field encoding and stable component IDs, not central bit assignments.
- Structured diagnostics exist for registry freeze and scene validation.
- A minimal migration/canonicalization framework exists for future schema changes; v1 runtime support is
  not required.
- Existing demo scenes are regenerated through a C++ schema-aware path.
- A test-only component registered outside core serialization round-trips through JSON and cook/dump.
- A tiny metadata/inspector proof can enumerate and edit fields through schema-aware authoring APIs.
- Full editor foundation remains deferred to the next phase.

## Retirement criteria

Remove or replace:

- central handwritten component codec table in `SceneSerialization.cpp`
- central cooked component bit enum/mask identity model
- runtime v1 scene JSON support
- handwritten Python scene JSON generation
- manual `Scene::register_components()` list as the authoritative Flecs registration path

## Open implementation details

- Exact C++ builder syntax.
- Stable hash algorithm choice for component IDs and registry fingerprints.
- Exact diagnostic class names and `Result` integration.
- Exact library target names for scene schema vs authoring slices.
- Whether demo generation is a `teng-scene-tool` subcommand or separate executable.
