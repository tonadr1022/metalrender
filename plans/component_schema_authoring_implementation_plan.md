# Component schema and authoring implementation plan

**Status:** Phase 9 sequencing plan. Slices 0-4 are implemented. Next: Slice 5, schema-driven JSON v2
validation and serialization. Architectural contract:
[`component_schema_authoring_model.md`](component_schema_authoring_model.md). Scene byte contract:
[`scene_serialization_design.md`](scene_serialization_design.md).

## Goal

Deliver Phase 9 in mergeable slices without preserving the current central serialization architecture.
Each slice should leave the repo verifiable and retire old paths as soon as their replacement is
proven.

Phase 9 exits only when schema-driven component registration, JSON v2, cooked v2, diagnostics,
authoring transactions, and C++ demo scene generation are all in place.

## Landed foundation

Completed slices established the migration boundary and should not grow more historical detail here.
Use code and tests as the source of truth for exact APIs.

- Slice 0: `component_schema_authoring_inventory.md` captured old serialization/cook/demo paths as a
  retirement checklist.
- Slice 1: `src/core/Diagnostic.*` added structured diagnostics without globally migrating `Result<T>`.
- Slice 2: `src/core/ComponentRegistry.*` added `ComponentRegistryBuilder`, frozen
  `ComponentRegistry`, stable component IDs, and freeze diagnostics.
- Slice 3: core component schemas gained declarative fields, typed defaults, visibility, asset/enum
  metadata, and validation hooks.
- Slice 4: `Scene` and `SceneManager` now require explicit `FlecsComponentContext`; core schema
  registration is split from Flecs runtime bindings through
  `register_core_components(ComponentRegistryBuilder&)` and
  `register_flecs_core_components(FlecsComponentContextBuilder&)`.

Current boundary: schema data belongs to the frozen `ComponentRegistry`; `FlecsComponentContext` is
only the scene-runtime Flecs binding context. Future slices must not put schema data back into the
Flecs context to make serialization convenient.

## Slice 5: Schema-driven JSON v2 validation and serialization

**Purpose:** Replace central component JSON logic with schema-driven canonical JSON v2.

Use the frozen `ComponentRegistry` directly for JSON validation/serialization and GPU-free tools. If
runtime startup needs both scene ticking and schema serialization, add explicit durable registry
ownership rather than embedding schema data in `FlecsComponentContext`.

Work:

- Implement JSON v2 envelope:
  - `scene_format_version`
  - `schema.registry_fingerprint`
  - `schema.required_modules`
  - `schema.components`
  - `scene`
  - `entities`
- Use namespaced component keys in entity component payloads.
- Generate complete component payloads from schema fields.
- Validate JSON through frozen registry and structured diagnostics.
- Emit canonical order: entity GUID, component key, field declaration order.
- Reject non-authored/runtime-only components in canonical payloads based on storage policy.
- Keep `teng-scene-tool` GPU-free.

Exit:

- Core scenes save/load as JSON v2.
- Runtime/tool load rejects v1, unknown component keys, unsupported modules, and unsupported component
  schema versions by default.
- The central JSON `ComponentCodec` table is removed or no longer authoritative.

Validation:

- Round-trip tests for all core authored components.
- Strict unknown component and runtime-only component rejection.
- Complete default field emission.
- Deterministic canonical JSON ordering.
- `./scripts/agent_verify.sh`.

## Slice 6: Test-module component extension proof

**Purpose:** Prove extensibility outside core serialization.

Work:

- Define a test-only component registrar outside the core component list.
- Include representative fields: numeric, bool or enum, and optionally `AssetId`.
- Save/load JSON v2 without editing central engine serialization tables.

Exit:

- Adding the test component requires only its registrar and tests.

Validation:

- JSON round-trip for the test component.
- Schema metadata enumeration for the test component.
- Diagnostics for loading a scene without the required module/component schema.

## Slice 7: Cooked scene v2 from schema fields

**Purpose:** Remove central cooked component bit/codec identity.

Work:

- Carry `binary_format_version`, `scene_format_version`, and schema compatibility metadata.
- Encode component records by stable component ID and component schema version.
- Encode supported fields in schema declaration order.
- Dump cooked data back to canonical JSON semantics.
- Remove fixed `ComponentBit` enum/mask as the global identity model.
- Allow local generated indexes/masks only if derived from registry/file contents.

Exit:

- Cook/dump works for core components and the test-module component.
- Old central cooked bit assignments are gone.

Validation:

- Cook/dump JSON parity for core fixture and test-module component.
- Unknown/newer component schema version rejection.

## Slice 8: Scene authoring library and transaction boundary

**Purpose:** Add the authoring surface that future editor and demo generation use.

Work:

- Add a GPU-free authoring/tooling library, e.g. `teng_scene_authoring` or equivalent.
- Add `SceneDocument` or equivalent edit-scene wrapper.
- Track document path/identity and dirty state.
- Add operation/transaction boundaries for authoring mutations.
- Add typed and schema-key APIs for entity creation, rename, component add/remove, and field edit.
- Route schema-key field edits through draft/validate/commit.
- Store enough before/after data for future undo, but do not implement the undo stack.

Exit:

- Authoring code can mutate scenes without direct editor writes into ECS.
- Dirty tracking and observable operation boundaries exist.

Validation:

- Dirty-state tests.
- Invalid field edit leaves scene unchanged.
- Typed operation fails when component type is not registered.
- Tiny metadata/inspector proof enumerates fields and commits one edit through the authoring API.

## Slice 9: C++ schema-aware demo scene generation

**Purpose:** Remove handwritten Python scene JSON construction.

Work:

- Add C++ generation path using the frozen registry and authoring API.
- Generate demo scene entities/components programmatically.
- Save scenes through the canonical schema serializer.
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
- `component_schema_authoring_inventory.md` is deleted or reduced to a short historical note.

Validation:

- `rg` confirms no remaining central-table names or old scene version symbols except migration comments
  or historical docs.
- `./scripts/agent_verify.sh`.

## Cross-slice invariants

- No new component should require editing a central serialization table.
- No authored component payload uses non-namespaced JSON keys.
- Runtime/editor/tool load stays strict by default.
- Scene save emits complete canonical payloads.
- Cooked component identity is stable per namespaced component key, not central enum order.
- Tools remain GPU-free unless a tool explicitly needs rendering.
- Runtime `Scene` remains separate from authoring `SceneDocument`.
- Full editor foundation, undo stack, hierarchy behavior, prefab system, and plugin hot-load remain out
  of Phase 9.

## Open details to decide during implementation

- Exact registry fingerprint input format and rendering.
- Exact target names for schema/authoring libraries.
- Whether demo generation is a `teng-scene-tool` subcommand or a separate executable.
