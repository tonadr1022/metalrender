# Component schema and authoring implementation plan

**Status:** Phase 9 sequencing plan. Slice 0 inventory and Slice 1 core diagnostics are implemented.
Next implementation slice: Slice 2, component registry builder and freeze. Architectural contract:
[`component_schema_authoring_model.md`](component_schema_authoring_model.md). Scene byte contract:
[`scene_serialization_design.md`](scene_serialization_design.md).

## Goal

Deliver Phase 9 in mergeable slices without preserving the current central serialization architecture.
Each slice should leave the repo in a verifiable state and retire old paths as soon as their replacement
is proven.

Phase 9 exits only when schema-driven component registration, JSON v2, cooked v2, diagnostics,
authoring transactions, and C++ demo scene generation are all in place.

## Slice 0: Inventory and fixtures

**Status:** Complete enough for Phase 9 continuation. Keep the inventory as a retirement checklist until
Slice 10 cleanup.

**Purpose:** Establish exact before-state and test fixtures before refactoring.

Current inventory: [`component_schema_authoring_inventory.md`](component_schema_authoring_inventory.md).

Work:

- Identify every current component serialization/cook site in `SceneSerialization.*`.
- Identify every scene-generation path that writes component JSON directly.
- Add or update tests that capture current valid core scene behavior before v2 replacement.
- Add a minimal test fixture scene with all current authored core components.

Exit:

- A short implementation note or checklist in the PR describes current central tables/code paths to
  retire.
- Existing smoke coverage still passes.

Validation:

- `./scripts/agent_verify.sh`

## Slice 1: Core diagnostics primitives

**Status:** Complete initial slice. `src/core/Diagnostic.*` now provides severity, stable string codes,
typed paths, reports, and render helpers. `tests/core/DiagnosticTests.cpp` covers stable rendering and
multi-diagnostic reports. Do not broaden this slice by migrating `Result<T>` call sites globally.

**Purpose:** Provide structured validation output before registry/schema validation starts.

Work:

- Add core diagnostic types, likely in `teng_core`:
  - `DiagnosticSeverity`
  - `DiagnosticCode` or stable string code wrapper
  - typed `DiagnosticPath` segments
  - `Diagnostic`
  - `DiagnosticReport`
- Add rendering helpers for CLI/test messages.
- Do not migrate all `Result<T>` call sites.

Exit:

- Registry/scene validation has a reusable report type that can collect multiple structured diagnostics.
- Diagnostics render stable codes and useful paths.

Validation:

- Unit tests for path rendering and multi-error reports.
- Existing `Result<T>` users remain unaffected.

## Slice 2: Component registry builder and freeze

**Status:** Next slice.

**Purpose:** Create the lifecycle boundary: mutable builder to immutable frozen registry.

Work:

- Add `ComponentRegistryBuilder`.
- Add immutable `ComponentRegistry`.
- Add module metadata registration.
- Add explicit registrar shape, e.g. `register_core_components(ComponentRegistryBuilder&)`.
- Implement freeze validation through `core::DiagnosticReport` for duplicate modules, duplicate
  component keys, duplicate field keys, invalid policies, and deterministic ordering.
- Add stable component ID generation from namespaced component keys, with collision diagnostics.
- Keep Flecs registration, field descriptors, JSON, cook, and scene construction on the existing paths
  until later slices consume the frozen registry.

Exit:

- A frozen registry can be built for core components, even before all serialization uses it.
- Production scene creation does not have to switch yet.
- Freeze failures return stable diagnostic codes and paths suitable for CLI/editor display.

Validation:

- Tests for duplicate component key, duplicate field key, invalid storage policy, and related module /
  component consistency failures.
- Freeze implements duplicate stable-component-ID detection when two distinct keys map to the same ID;
  no test-only collision injection hook (64-bit collisions are impractical to fabricate without heavy
  boilerplate).
- No dedicated registry smoke in `engine_scene_smoke` for this slice; registrar happy-path integration
  lands when the frozen registry is consumed (Slice 4+).
- Diagnostics use stable codes, not message matching only.
- Existing scene smoke and serialization behavior still pass unchanged.

## Slice 3: Declarative field schema for core components

**Purpose:** Define one schema source for current engine components.

Work:

- Add field descriptors for bool, integer, float, string, vec2/vec3/vec4, quaternion, `AssetId`, and
  minimal enum metadata.
- Register current components with policies:
  - `teng.core.transform` as authored/default-on-create
  - `teng.core.camera` as authored
  - `teng.core.directional_light` as authored
  - `teng.core.mesh_renderable` as authored
  - `teng.core.sprite_renderable` as authored
  - `LocalToWorld` as runtime-derived
  - `FpsCameraController` as runtime-session/debug-inspectable if included
  - `EngineInputSnapshot` as hidden runtime-session
- Add default extraction from C++ default construction.
- Add component-local validation hook support.

Exit:

- Core schema can enumerate components, fields, defaults, storage policy, and cooked IDs.
- No JSON/cook behavior has to switch yet, but schemas are authoritative enough to do so.

Validation:

- Tests for field order, default values, storage policies, and component-local validation.
- Asset reference fields validate syntax separately from project asset existence.

## Slice 4: Registry-driven Flecs registration and scene context

**Purpose:** Remove manual Flecs component registration as the authoritative list.

Work:

- Introduce a frozen scene/component context used by `Scene` and `SceneManager`.
- Require explicit context/registry for production scene construction.
- Drive Flecs component registration from the frozen registry.
- Move default-on-create policy into schema/context:
  - `Transform` added for normal entities.
  - `LocalToWorld` added as runtime-derived where required.
- Keep system registration separate from component registry.

Exit:

- `Scene::register_components()` no longer owns a hand-maintained core component list.
- A `SceneManager` cannot accidentally mix scenes from different registries.

Validation:

- Scene smoke still passes.
- Tests prove creating a scene without required context is not a production path.
- Tests prove default-on-create and optional component absence behavior.

## Slice 5: Schema-driven JSON v2 validation and serialization

**Purpose:** Replace central component JSON logic with schema-driven canonical JSON v2.

Work:

- Implement JSON v2 envelope:
  - `scene_format_version`
  - `schema.registry_fingerprint`
  - `schema.required_modules`
  - `schema.components`
  - `scene`
  - `entities`
- Use namespaced component keys in `components`.
- Generate complete component payloads from schema fields.
- Validate JSON through frozen registry and diagnostics.
- Emit canonical order:
  - entity GUID order
  - component key sort order
  - field declaration order
- Reject runtime-only components in canonical payloads.

Exit:

- Core scenes save/load as JSON v2.
- Runtime/editor load rejects v1/unknown component keys by default.
- The central JSON `ComponentCodec` table is removed or no longer authoritative.

Validation:

- Round-trip tests for all core authored components.
- Strict unknown component rejection.
- Runtime-only component rejection.
- Complete default field emission.
- Deterministic canonical JSON ordering.

## Slice 6: Test-module component extension proof

**Purpose:** Prove extensibility outside core serialization.

Work:

- Define a test-only component in a test/smoke TU or test helper module.
- Register it through a non-core registrar.
- Include representative fields: numeric, bool or enum, and optionally `AssetId`.
- Create a scene using this component through the registry.
- Save/load JSON v2 without editing central engine serialization tables.

Exit:

- Adding this component requires one registration site and test code, not modifications to
  `SceneSerialization.cpp` component tables.

Validation:

- JSON round-trip for test component.
- Schema metadata enumeration for test component.
- Diagnostics for missing module/component schema if loading without its registrar.

## Slice 7: Cooked scene v2 from schema fields

**Purpose:** Remove central cooked component bit/codec identity.

Work:

- Carry `binary_format_version`, `scene_format_version`, and schema compatibility metadata.
- Encode component records by stable component ID and component schema version.
- Encode supported fields in schema declaration order.
- Dump cooked data back to canonical JSON semantics.
- Remove fixed `ComponentBit` enum/mask as global identity model.
- Allow local generated indexes/masks only if derived from registry/file contents.

Exit:

- Cook/dump works for core components and the test-module component.
- Old central cooked bit assignments are gone.

Validation:

- Cook/dump JSON parity for core fixture.
- Cook/dump parity for test-module component.
- Unknown/newer component schema version rejection.

## Slice 8: Scene authoring library and transaction boundary

**Purpose:** Add the authoring surface that future editor and demo generation use.

Work:

- Add a GPU-free authoring/tooling slice, e.g. `teng_scene_authoring` or equivalent.
- Add `SceneDocument` or equivalent wrapper around the edit scene.
- Track document path/identity and dirty state.
- Add operation/transaction boundary for authoring mutations.
- Add typed and schema-key scene-builder APIs:
  - create entity
  - rename entity
  - add/remove component
  - set field by key
  - typed component set where useful
- Route schema-key field edits through draft/validate/commit.
- Store enough before/after data for future undo, but do not implement undo stack.

Exit:

- Authoring code can mutate scenes without direct editor writes into ECS.
- Dirty tracking exists.
- Future undo has observable operation boundaries.

Validation:

- Tests for dirty state.
- Tests for rejected invalid field edit leaving scene unchanged.
- Tests for typed operation failing when component type is not registered.
- Tiny metadata/inspector proof enumerates fields and commits one edit through the authoring API.

## Slice 9: C++ schema-aware demo scene generation

**Purpose:** Remove handwritten Python scene JSON construction.

Work:

- Add C++ generation path using the frozen registry and authoring API.
- Generate demo scene worlds/entities/components programmatically.
- Save scenes through canonical schema serializer.
- Keep Python only for non-scene asset orchestration or invoking C++ tools, if still useful.
- Regenerate current demo scenes as JSON v2.

Exit:

- `scripts/generate_demo_scene_assets.py` no longer handwrites canonical scene component JSON.
- Demo scenes are v2 and load through the new runtime path.

Validation:

- Generated scene assets smoke test updated for v2.
- `metalrender --scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30`.
- `./scripts/agent_verify.sh`.

## Slice 10: Cleanup and retirement

**Purpose:** Delete old architecture and stale compatibility paths.

Work:

- Remove runtime v1 scene load assumptions.
- Remove old `registry_version` semantics from code.
- Remove old central component codec tables.
- Remove old central cooked component bit identity.
- Remove stale Python scene JSON helpers.
- Update docs if implementation differs from plan names.

Exit:

- The only supported runtime scene path is schema-driven JSON v2 / cooked v2.
- Component addition path is documented and covered by the extension proof.

Validation:

- `rg` confirms no remaining central-table names or old scene version symbols except migration comments
  or historical docs.
- `./scripts/agent_verify.sh`.

## Dependency order

Recommended order:

1. diagnostics (done)
2. registry builder/freeze (next)
3. core schemas
4. Scene/Flecs context integration
5. JSON v2
6. extension proof
7. cooked v2
8. authoring library
9. demo generation
10. cleanup

The extension proof should land before declaring the JSON/cook architecture done. Authoring/demo work
should consume the schema serializer rather than creating parallel payload construction.

## Cross-slice invariants

- No new component should require editing a central serialization table.
- No authored component payload uses non-namespaced JSON keys.
- Runtime/editor load stays strict by default.
- Scene save emits complete canonical payloads.
- Cooked component identity is stable per namespaced component key, not central enum order.
- Tools remain GPU-free unless a tool explicitly needs rendering.
- Runtime `Scene` remains separate from authoring `SceneDocument`.
- Full editor foundation, undo stack, hierarchy behavior, prefab system, and plugin hot-load remain out
  of Phase 9.

## Open details to decide during implementation

- Exact C++ builder names and fluent syntax.
- Exact stable hash implementation.
- Exact registry fingerprint input format and rendering.
- Exact target names for schema/authoring libraries.
- Whether demo generation is a `teng-scene-tool` subcommand or a separate executable.
- Whether `FpsCameraController` gets full field metadata in the first pass or opaque runtime-session
  registration.
