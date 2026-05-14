# Component schema and authoring implementation plan

**Status:** Phase 9 sequencing plan. Slices 0–7 are implemented. Next: **Slice 8**, scene authoring
library and transaction boundary. Scene byte contract: [`scene_serialization_design.md`](scene_serialization_design.md).

## Goal

Deliver Phase 9 in mergeable slices without preserving the current central serialization architecture.
Each slice should leave the repo verifiable and retire old paths as soon as their replacement is
proven.

Phase 9 exits when schema-driven component registration, JSON v2, cooked v2, diagnostics, and a
small authoring transaction boundary are in place. Demo generation may move to a Luau/runtime proof
instead of a C++ scene generator if that provides faster gameplay iteration.

## Landed foundation

Completed slices established the migration boundary and should not grow more historical detail here.
Use code and tests as the source of truth for exact APIs.

- Slice 1: `src/core/Diagnostic.*` added structured diagnostics without globally migrating `Result<T>`.
- Slice 2: `src/engine/scene/ComponentRegistry.*` added frozen `ComponentRegistry`, stable
  component IDs, and freeze diagnostics.
- Slice 3: core component schemas gained declarative fields, typed defaults, visibility, asset/enum
  metadata, and validation hooks.
- Slice 4: `Scene` and `SceneManager` now require explicit `FlecsComponentContext`.
- Slice 5: central component JSON logic was replaced with schema-driven canonical JSON v2.
- Slice 6: a generated test-extension component round-trips without editing core serialization code.
- Slice 6.5: Clang component reflection codegen now emits `ComponentModuleDescriptor` data that
  freezes directly into the registry; old builder/adaptor registration streams are gone.
- Slice 7: cooked scene v2 from registry field metadata — `SceneCooked.*`, `BinaryReader`/`BinaryWriter`/
  `CookedArtifact`, `teng-scene-tool` **cook**/**dump**, `tests/smoke/SceneCookedTests.cpp`; no central
  cooked component bit table.

Current boundary: schema data belongs to the frozen `ComponentRegistry`; `FlecsComponentContext` is
only the scene-runtime Flecs binding context. `SceneSerializationContext` references the same frozen
registry. Future slices must not duplicate schema facts into side tables to make serialization,
cooking, or editor work convenient.

## Slice 6: Test-module component extension proof

**Status:** implemented.

**Purpose:** Prove extensibility outside core serialization.

Landed:

- Test-only component declarations live outside the core component list.
- The generated test module includes representative fields and JSON ops.
- Tests compose core and test-extension module descriptor spans before registry freeze.
- JSON v2 save/load works without editing central engine serialization tables.

Exit:

- Adding the test component requires only its registrar and tests.

Validation:

- JSON round-trip for the test component.
- Schema metadata enumeration for the test component.
- Diagnostics for loading a scene without the required module/component schema.

## Slice 7: Cooked scene v2 from schema fields

**Status:** implemented.

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

## Slice 8: Reduced scene authoring library and transaction boundary

**Purpose:** Add the smallest authoring surface that future editor tools, scripts, and scene
generators can share without writing directly into authored ECS state.

Work:

- Add a GPU-free authoring/tooling library, e.g. `teng_scene_authoring` or equivalent.
- Add `SceneDocument` or equivalent edit-scene wrapper.
- Track document path/identity and dirty state.
- Add lightweight operation/transaction boundaries for authoring mutations.
- Add typed APIs for entity create, rename, destroy, component add/remove, and whole-component set.
- Add schema-key field edit as draft JSON payload -> validate -> commit, without adding a universal
  generated per-field setter surface in this slice.
- Record operation metadata that can later feed undo/redo, but do not implement undo snapshots or an
  undo stack.

Exit:

- Authoring code can mutate scenes without direct editor writes into ECS.
- Dirty tracking and observable operation boundaries exist.
- Invalid schema-key edits leave the scene unchanged.

Validation:

- Dirty-state tests.
- Invalid field edit leaves scene unchanged.
- Typed operation fails when component type is not registered.
- Tiny metadata/inspector proof enumerates fields and commits one schema-key edit through the
  authoring API.

## Slice 9: Demo authoring path decision

**Purpose:** Remove handwritten Python scene JSON construction without over-investing in C++ scene
generation if Luau runtime scripting is the better iteration path.

Work:

- Decide whether demo scene construction should be a C++ tool, a Luau/runtime proof, or a minimal
  bridge that invokes one of those paths.
- If C++ wins, generate demo scene entities/components through the authoring API and save through the
  canonical schema serializer.
- If Luau wins, use this slice to define the smallest runtime scripting proof needed for demos that
  add/set components and drive a camera walk-around scene.
- Keep Python only for non-scene asset orchestration or invoking C++ tools, if still useful.
- Regenerate or replace current demo scenes as JSON v2/runtime-authored equivalents.

Exit:

- `scripts/generate_demo_scene_assets.py` no longer handwrites canonical scene component JSON.
- Demo scenes are v2 and load through the new runtime path, or the Luau proof produces equivalent
  runtime demo coverage.

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
- Whether demo generation becomes a `teng-scene-tool` subcommand, a separate executable, or a Luau
  runtime demo path.
