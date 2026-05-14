# Phase 9 Slice 7: Cooked scene v2 from schema fields

**Status:** **Complete** — cooked v2 shipped: `SceneCooked.*`, `src/engine/content/BinaryReader.*` /
`BinaryWriter.*` / `CookedArtifact.*`, `SceneSerialization` cook/dump delegation, `teng-scene-tool`
**cook**/**dump**, `tests/smoke/SceneCookedTests.cpp`.

**Parent plan:** [`component_schema_authoring_implementation_plan.md`](component_schema_authoring_implementation_plan.md).
**Architecture contract:** [`component_schema_authoring_model.md`](component_schema_authoring_model.md).
**Scene byte contract:** [`scene_serialization_design.md`](scene_serialization_design.md).

## Purpose

Replace the disabled cooked-scene path with a v2 binary format derived from the frozen
`ComponentRegistry`, not from fixed central component enum bits or handwritten cooked component
tables.

Slice 7 exits when `teng-scene-tool cook` and `teng-scene-tool dump` work for core authored
components plus the Slice 6 test-module component, and cook/dump preserves canonical JSON semantics.

## Context at slice start (historical)

The bullets below described the repo **before** Slice 7 landed; they are kept only as a planning paper
trail.

- `ComponentRegistry` already froze sorted component records, module/schema versions, and `stable_id`
  via `stable_component_id_v1(component_key)`.
- JSON v2 exposed field declaration order and authored vs runtime-only rejection.
- Cooked entry points in `SceneSerialization` were stubbed/disabled; `teng-scene-tool` **cook**/**dump**
  printed unsupported.

## Implementation pointers (current)

- Cooked format and I/O: `src/engine/scene/SceneCooked.hpp`, `SceneCooked.cpp`.
- Shared binary envelope helpers: `src/engine/content/BinaryReader.*`, `BinaryWriter.*`,
  `CookedArtifact.*`.
- Public cook/dump API surface: `SceneSerialization.hpp` / `SceneSerialization.cpp` (delegates to
  `SceneCooked`).
- CLI: `apps/teng-scene-tool/main.cpp` — **cook**, **dump**.
- Tests: `tests/smoke/SceneCookedTests.cpp`.

## Dependencies and assumptions

- JSON v2 has landed enough functionality that `validate_scene_file_full_report` is the strict
  validation gate for cooking.
- Slice 6 has a test-only generated module outside core, so the cook/dump tests can prove extension
  without editing a central cooked component table.
- Cooked v2 is little-endian only, matching `scene_serialization_design.md`.
- Phase 9 does not preserve old cooked binaries. Unknown/newer cooked versions fail fast with a clear
  error.

## Files and targets

### Add

- `src/engine/scene/SceneCooked.hpp`
  - Public cooked-v2 constants, lightweight binary structs, and `CookedSceneOptions` if needed.
- `src/engine/scene/SceneCooked.cpp`
  - Binary reader/writer, schema compatibility metadata encoding, field encoding/decoding, and
    JSON-dump reconstruction.
- `tests/smoke/SceneCookedTests.cpp`
  - Core cook/dump parity, test-module parity, and failure cases.

### Change

- `src/engine/scene/SceneSerialization.hpp`
  - Keep the public cook/dump function declarations, but make them require a
    `const SceneSerializationContext&` or a smaller schema codec context.
  - Retire or rename `k_scene_binary_format_version` so the cooked version constant lives with cooked
    v2.
- `src/engine/scene/SceneSerialization.cpp`
  - Remove unsupported cooked stubs.
  - Delegate cook/dump entry points to `SceneCooked.cpp`.
  - Share only JSON validation/canonicalization helpers that belong in the JSON layer; do not duplicate
    scene envelope validation in the cooked layer.
- `apps/teng-scene-tool/main.cpp`
  - Build the same registry/serialization context used by validation and call cook/dump.
- `src/CMakeLists.txt`
  - Add `SceneCooked.cpp` to `TENG_SCENE_SOURCES`.
- `tests/CMakeLists.txt`
  - Add `SceneCookedTests.cpp` to `teng_engine_tests`.

### Remove or avoid

- Any fixed global `ComponentBit` enum, global component mask assignment, or central cooked component
  order table.
- Any cooked path that knows component identity by C++ type rather than registry stable ID.
- Any requirement for render/gfx linkage in `teng-scene-tool`.

## Public API shape

Prefer explicit context parameters so cooking uses the frozen registry selected by the caller:

```cpp
Result<std::vector<std::byte>> cook_scene_to_memory(
    const SceneSerializationContext& serialization,
    const nlohmann::json& scene_json);

Result<void> cook_scene_file(
    const SceneSerializationContext& serialization,
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path);

Result<nlohmann::ordered_json> dump_cooked_scene_to_json(
    const SceneSerializationContext& serialization,
    std::span<const std::byte> bytes);

Result<void> dump_cooked_scene_file(
    const SceneSerializationContext& serialization,
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path);
```

If this makes `SceneSerialization.hpp` too broad, move these declarations to `SceneCooked.hpp` and
leave compatibility wrappers only where existing callers need them.

## Binary format

Use a simple deterministic v2 container first; optimize later only when profiling demands it.

Header:

- magic: `TSCNCOOK`
- `binary_format_version`: `2`
- `scene_format_version`: `2`
- endian marker: fixed little-endian marker
- section offsets/sizes for string table, schema metadata, scene metadata, entities, and component
  payloads

Schema metadata:

- registry fingerprint if available; otherwise store an explicit placeholder string and mark
  fingerprint finalization as retirement work.
- required modules: sorted module ID string table index plus module version.
- required components: stable component ID, component key string table index, schema version, and
  module string table index.

Entity records:

- `EntityGuid::value` as `uint64_t`.
- optional entity name string table index, with `0` or an invalid sentinel for no name.
- component record range.

Component records:

- stable `uint64_t component_id`.
- `uint32_t schema_version`.
- payload offset and payload byte length.
- no global component bit, no global component ordinal. A per-file sorted component range is fine.

Field payload:

- fields encoded in `FrozenComponentRecord::fields` order.
- no per-field names inside the canonical field blob.
- primitive encodings:
  - `Bool`: `uint8_t` 0/1
  - `I32`: little-endian `int32_t`
  - `U32`: little-endian `uint32_t`
  - `F32`: little-endian IEEE-754 `float`
  - `String`: string table index
  - `Vec2`/`Vec3`/`Vec4`/`Quat`: contiguous little-endian floats
  - `Mat4`: 16 contiguous little-endian floats
  - `AssetId`: two little-endian `uint64_t` values, decoded back to canonical string on dump
  - `Enum`: stable authored enum key encoded as string table index for v2

## Cook flow

1. Parse JSON file through existing JSON IO.
2. Validate through `validate_scene_file_full_report(serialization, scene_json)`.
3. Canonicalize before binary emission:
   - Prefer a small `canonicalize_scene_json(serialization, scene_json)` helper if one is available.
   - If no helper exists yet, deserialize into a `Scene` and reserialize, or implement a narrowly scoped
     canonical ordered-json builder that uses the same registry rules.
4. Build deterministic schema metadata from the actual components used by the canonical document.
5. Build the string table from scene name, entity names, module IDs, component keys, string fields, enum
   keys, and any diagnostic metadata.
6. For every entity sorted by GUID, encode authored component payloads sorted by component key.
7. For every component, look up the frozen record by key and encode fields by declaration order.
8. Write the complete byte vector only after all validation and offset calculations succeed.

## Dump flow

1. Read and validate the cooked header, version, endian marker, and section bounds.
2. Load schema metadata and check:
   - registered module exists and version is supported
   - registered component stable ID resolves to exactly one frozen component
   - component key matches the stable ID metadata
   - component schema version equals the frozen schema version
3. Decode entity records and component records.
4. Decode component field blobs through the frozen schema record.
5. Reconstruct canonical ordered JSON:
   - `scene_format_version`
   - `schema` metadata in the same key shape as JSON v2
   - `scene.name`
   - sorted `entities`
   - sorted component keys and field declaration order
6. Validate the dumped JSON through the normal JSON validator before returning it.

## Error handling and diagnostics

- Public cook/dump APIs may keep returning `Result<T, std::string>` for now, matching existing scene
  serialization boundaries.
- Internally, prefer `DiagnosticReport` for schema/version/data failures where multiple diagnostics are
  practical.
- Fail immediately for corrupted binary structure, out-of-bounds sections, truncated payloads, or
  impossible invariants.
- Include component key and stable ID in errors when an ID is unknown or metadata mismatches.

## Test additions

- Core fixture cook/dump parity:
  - Build a JSON v2 scene containing `Transform`, `Camera`, `DirectionalLight`, `MeshRenderable`, and
    `SpriteRenderable`.
  - Cook to memory, dump to JSON, validate, and compare canonical semantics to the canonicalized input.
- Test-module parity:
  - Use the Slice 6 generated test component module descriptors.
  - Confirm no edits to core registrars or cooked tables are required.
- Version rejection:
  - Mutate `binary_format_version` to an unsupported value.
  - Mutate a component schema version to a newer value.
  - Mutate a stable component ID to an unknown value.
- Runtime-only rejection:
  - Attempt to cook JSON containing `teng.core.local_to_world`; validation should reject before binary
    emission.
- CLI smoke:
  - Invoke `teng-scene-tool cook in.tscene.json out.tscene.bin`.
  - Invoke `teng-scene-tool dump out.tscene.bin dumped.tscene.json`.
  - Validate dumped JSON.

## Validation command

```bash
./scripts/agent_verify.sh
```

For faster iteration while implementing:

```bash
cmake --build --preset Debug --target teng_engine_tests teng-scene-tool
./build/Debug/bin/teng_engine_tests
./build/Debug/bin/teng-scene-tool validate <dumped.tscene.json>
```

## Migration scaffolding and retirement criteria

- Keep cooked v2 as the only supported cooked binary format.
- Do not add v1 cooked compatibility shims.
- If local compact indexes or masks are useful, generate them per file from sorted stable component IDs
  and keep them private to the cooked payload.
- Retire any temporary canonicalization-by-load-resave path once the JSON serializer exposes a direct
  canonicalization helper.

## Non-goals

- Full asset bundle cooking or project-wide dependency closure.
- Field arrays, maps, variants, nullable values, or flags.
- Component schema migrations beyond strict current-version acceptance.
- Unknown component preservation or tolerant editor import.
- Runtime player save format.
- Editor authoring transactions, undo stack, or inspector UI.

## Risks

- **Schema metadata name drift:** 
  use `schema.required_components`.
- **Canonicalization duplication:** cooked code can accidentally duplicate JSON ordering rules. Prefer a
  shared helper where possible.
- **String table determinism:** include strings in first-use canonical order or globally sorted order,
  but document and test whichever rule is chosen.
- **AssetId representation:** binary should store parsed IDs, while dump must reproduce canonical string
  form.
- **Extension proof weakness:** if the test-module component still needs central serialization edits,
  Slice 7 should stop and finish the extension path first.

## Open questions

- Should `SceneCooked.hpp` be public under `teng_scene`, or should cook/dump stay public only through
  `SceneSerialization.hpp`?
- Should registry fingerprint be mandatory in the first cooked v2 header, or allowed as an empty string
  until the fingerprint input is finalized?
- Should enum fields encode authored keys for readability/stability or numeric values for compactness?
  The plan chooses authored keys for v2 to match JSON semantics and avoid future enum renumbering risk.
