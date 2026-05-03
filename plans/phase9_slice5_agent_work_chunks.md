# Phase 9 Slice 5: Agent work chunks

**Status:** Execution companion for
[`phase9_slice5_schema_driven_json_v2_plan.md`](phase9_slice5_schema_driven_json_v2_plan.md).

## Read first

- `AGENTS.md`
- [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md)
- [`phase9_slice5_schema_driven_json_v2_plan.md`](phase9_slice5_schema_driven_json_v2_plan.md)

## Coordination rules

- The main Slice 5 plan is authoritative for behavior, validation rules, non-goals, and retirement
  criteria.
- Do not implement cooked v2, scene migration tooling, `SceneDocument`, authoring transactions,
  editor UI, demo generation, or the Slice 6 test-module extension proof.
- Chunks that touch `src/engine/scene/SceneSerialization.*` should land sequentially or have one
  explicit integration owner because they will conflict.
- Prefer small compileable steps. Do not leave two authoritative component-key systems at the end of
  the slice.
- Run chunk-local validation while developing. The integrated slice exits on `./scripts/agent_verify.sh`.

## Chunk 1: Registry module retention

**Owns:**

- `src/core/ComponentRegistry.hpp`
- `src/core/ComponentRegistry.cpp`
- `tests/core/ComponentRegistryTests.cpp`

**Goal:** Retain deterministic frozen module records in `ComponentRegistry` so JSON v2 validation and
schema export can reason from the frozen registry instead of the builder.

**Depends on:** Nothing.

**Tasks:**

- Add a frozen module record type, or equivalent, with module id and version.
- Store sorted frozen module records in `ComponentRegistry`.
- Expose deterministic `modules()` and `find_module()` APIs.
- Preserve existing duplicate module and module version mismatch diagnostics.
- Keep `teng_core` free of JSON dependencies.

**Acceptance:**

- Frozen modules are sorted by module id.
- Lookup returns the frozen module version.
- Existing duplicate and version mismatch behavior still fails freeze.
- Tests cover sorting, lookup, and duplicate/version behavior.

## Chunk 2: Schema metadata JSON export

**Owns:**

- `src/engine/scene/ComponentSchemaJson.hpp`
- `src/engine/scene/ComponentSchemaJson.cpp`
- `src/engine/scene/SceneSerialization.cpp`
- `src/CMakeLists.txt`
- focused schema export tests

**Goal:** Move the hidden schema metadata exporter out of `SceneSerialization.cpp` and make it a
reusable scene/tool-layer operation.

**Depends on:** Chunk 1.

**Tasks:**

- Add `serialize_component_schema_to_json(const core::ComponentRegistry&)`.
- Move the current `serialize_schema_to_json` behavior into `ComponentSchemaJson.*`.
- Include frozen module records in the exported schema metadata.
- Keep canonical scene-file load/save separate from schema metadata export.

**Acceptance:**

- `SceneSerialization.cpp` no longer owns hidden schema-export logic.
- `ComponentSchemaJson.*` lives in `teng_scene`.
- Tests can export built-in schema metadata without loading or saving a scene file.

## Chunk 3: Serialization context and built-in binding skeleton

**Owns:**

- `src/engine/scene/SceneSerializationContext.hpp`
- `src/engine/scene/SceneSerializationContext.cpp`
- `src/engine/scene/BuiltinSceneSerialization.hpp`
- `src/engine/scene/BuiltinSceneSerialization.cpp`
- `src/CMakeLists.txt`

**Goal:** Add a serialization context sibling to `FlecsComponentContext`, backed by the frozen
registry and keyed by namespaced component keys.

**Depends on:** Chunk 1.

**Tasks:**

- Add `SceneComponentJsonBinding` with typed `has`, `serialize`, and `deserialize` function pointers.
- Add `SceneSerializationContextBuilder`.
- Validate that each binding key exists in the frozen registry.
- Validate canonical bindings only target `Authored` components.
- Validate every built-in authored component has a binding when freezing the built-in context.
- Make binding registration order irrelevant for canonical ordering.
- Add `register_builtin_scene_serialization(SceneSerializationContextBuilder&)`.

**Acceptance:**

- Builder rejects unknown component keys.
- Builder rejects canonical bindings for runtime-only components.
- Built-in context freezes only when all built-in authored components have bindings.
- No JSON v2 save/load rewrite is required in this chunk.

## Chunk 4: Typed built-in component JSON bindings

**Owns:**

- `src/engine/scene/BuiltinSceneSerialization.hpp`
- `src/engine/scene/BuiltinSceneSerialization.cpp`
- component binding tests, if separated from smoke tests

**Goal:** Implement typed JSON bindings for built-in authored scene components without using the old
central `ComponentCodec` vocabulary.

**Depends on:** Chunk 3.

**Tasks:**

- Implement bindings for `teng.core.transform`.
- Implement bindings for `teng.core.camera`.
- Implement bindings for `teng.core.directional_light`.
- Implement bindings for `teng.core.mesh_renderable`.
- Implement bindings for `teng.core.sprite_renderable`.
- Keep runtime-only/debug components out of canonical bindings.
- Return `Result` from typed parse/apply functions so malformed data can still fail at apply time.

**Acceptance:**

- Each built-in authored component can be serialized from a Flecs entity through the context.
- Each built-in authored component can be deserialized onto a Flecs entity through the context.
- Short keys such as `transform` are not introduced in the new binding path.

## Chunk 5: Schema-driven JSON v2 validation

**Owns:**

- `src/engine/scene/SceneSerialization.hpp`
- `src/engine/scene/SceneSerialization.cpp`
- validation-focused tests

**Goal:** Validate JSON v2 scene files from registry metadata and serialization bindings before scene
creation.

**Depends on:** Chunks 1 and 3. Chunk 4 is needed for typed apply coverage, but pure shape validation
can start earlier if coordinated.

**Tasks:**

- Change validation APIs to accept `const SceneSerializationContext&`.
- Validate top-level `scene_format_version == 2`.
- Reject `registry_version` and unknown top-level, `schema`, `scene`, entity, component, and field
  keys.
- Validate exact/minimal `schema.required_modules` and `schema.required_components`.
- Validate required non-empty `scene.name`.
- Validate fixed-width lowercase hex entity GUID strings.
- Require `entities[].components` to be an object, including `{}` for empty entities.
- Validate component keys exist, are `Authored`, and match supported schema versions.
- Validate complete field sets from `FrozenComponentRecord::fields`.
- Validate JSON field kinds, enum stable keys, and `AssetId` text syntax.
- Produce structured diagnostics internally and stringify at the existing public `Result` boundary.

**Acceptance:**

- Invalid v1 files fail validation.
- Unknown, missing, or wrong-type fields fail validation before scene creation.
- Runtime-only payload components such as `teng.core.local_to_world` fail validation.
- Load/save behavior can still be incomplete in this chunk if validation tests are isolated.

## Chunk 6: JSON v2 save

**Owns:**

- `src/engine/scene/SceneSerialization.hpp`
- `src/engine/scene/SceneSerialization.cpp`
- save/ordering tests

**Goal:** Emit canonical JSON v2 from a scene using `SceneSerializationContext`.

**Depends on:** Chunks 3 and 4. Chunk 5 is strongly preferred first.

**Tasks:**

- Change `serialize_scene_to_json` and `save_scene_file` to accept `const SceneSerializationContext&`.
- Emit `scene_format_version`.
- Emit exact/minimal `schema.required_modules` and `schema.required_components` from serialized
  components.
- Emit required `scene.name`.
- Emit fixed-width lowercase hex entity GUID strings.
- Emit optional non-empty entity names.
- Emit required `components` objects with namespaced component keys.
- Emit complete component field payloads, including defaults.
- Sort entities by `EntityGuid::value`, components by key, modules by id, and required components by
  key.
- Use `nlohmann::ordered_json` at the canonical output boundary.

**Acceptance:**

- Saved files contain no `registry_version`.
- Saved files contain no short component keys.
- Repeated saves are deterministic.
- Field order follows registry declaration order.

## Chunk 7: JSON v2 load

**Owns:**

- `src/engine/scene/SceneSerialization.hpp`
- `src/engine/scene/SceneSerialization.cpp`
- load/round-trip tests

**Goal:** Load validated JSON v2 into ECS scenes through typed bindings.

**Depends on:** Chunks 4 and 5. Chunk 6 is preferred first for round-trip tests.

**Tasks:**

- Change `deserialize_scene_json` and `load_scene_file` to accept `const SceneSerializationContext&`.
- Validate the full document before creating a scene.
- Create the scene only after validation succeeds.
- Create entities by GUID and optional name.
- Let add-on-create defaults run first, then apply authored component payloads.
- Recompute `LocalToWorld` after load.
- Destroy the partially created scene if an apply-time failure still occurs.
- Accept non-canonical object order on load and rely on save to normalize ordering.

**Acceptance:**

- JSON v2 round-trip works through ECS.
- Authored component values overwrite add-on-create defaults.
- Render extraction smoke still sees cameras, lights, meshes, and sprites after load.

## Chunk 8: Runtime context ownership wiring

**Owns:**

- `src/engine/Engine.hpp`
- `src/engine/Engine.cpp`

**Goal:** Retain the frozen registry and scene serialization context for the engine process lifetime.

**Depends on:** Chunks 3 and 7.

**Tasks:**

- Add `std::unique_ptr<core::ComponentRegistry> frozen_component_registry_`.
- Add `std::unique_ptr<SceneSerializationContext> scene_serialization_ctx_`.
- Build the registry once during `Engine::init()`.
- Build both Flecs and serialization contexts from the same frozen registry.
- Pass `scene_serialization_ctx_` into runtime scene load calls.
- Avoid rebuilding registries inside load/save helpers.

**Acceptance:**

- Runtime startup scene loading uses the explicit serialization context.
- The frozen registry outlives both Flecs and serialization contexts.
- No renderer, platform, or asset-service dependency is added to `teng_scene`.

## Chunk 9: Tests and shared test context bundle

**Owns:**

- `tests/smoke/TestHelpers.hpp`
- `tests/smoke/TestHelpers.cpp`
- `tests/smoke/SceneSerializationSmokeTest.cpp`
- `tests/smoke/GeneratedSceneAssetsSmokeTest.cpp`
- resource scene fixtures as needed

**Goal:** Convert first-party scene serialization tests and fixtures to JSON v2 and make tests share
one frozen registry/Flecs/serialization context bundle.

**Depends on:** Chunks 3, 6, and 7.

**Tasks:**

- Replace `make_scene_component_context()`-only helpers with a helper that owns the frozen registry,
  `FlecsComponentContext`, and `SceneSerializationContext`.
- Convert smoke scene text to JSON v2 namespaced keys.
- Update generated-scene tests to pass the serialization context.
- Remove cooked v1 round-trip expectations.
- Add rejection coverage from the main plan where it naturally fits.
- Keep render extraction coverage after load.

**Acceptance:**

- `teng_engine_tests` and `engine_scene_smoke` pass against JSON v2.
- Tests no longer rely on v1 `registry_version` input except rejection tests.
- Tests no longer require cooked v1 behavior.

## Chunk 10: Scene tool surface and v1/cooked retirement

**Owns:**

- `apps/teng-scene-tool/main.cpp`
- `src/engine/scene/SceneSerialization.hpp`
- `src/engine/scene/SceneSerialization.cpp`
- tool validation tests if present

**Goal:** Keep `teng-scene-tool validate` GPU-free and schema-backed while removing or explicitly
disabling unsupported v1 cooked/migration surfaces.

**Depends on:** Chunks 3, 5, and 7.

**Tasks:**

- Build and freeze the built-in component registry in the CLI process.
- Build `SceneSerializationContext` without creating a window, renderer, asset service, or Flecs
  world unless a subcommand genuinely needs ECS load.
- Make `validate` call the same JSON v2 validation pipeline as runtime load.
- Remove `migrate`, or leave it as a clear unsupported diagnostic.
- Remove `cook` and `dump`, or leave them as clear unsupported diagnostics until cooked v2 work.
- Remove public cooked v1 APIs if no longer needed by call sites.

**Acceptance:**

- `teng-scene-tool validate <v2 scene>` succeeds for valid v2 scenes.
- Malformed v2 files produce human-readable diagnostics.
- `cook`, `dump`, and `migrate` do not preserve v1 behavior silently.
- `teng-scene-tool` remains GPU-free.

## Chunk 11: Final cleanup pass

**Owns:** Cross-cutting cleanup after prior chunks merge.

**Goal:** Remove leftover v1 authority and temporary duplicate vocabulary.

**Depends on:** Chunks 1 through 10.

**Tasks:**

- Delete the old central `ComponentCodec` authority.
- Remove short-key scene load/save support.
- Remove v1 `registry_version` runtime/tool acceptance.
- Remove unused cooked v1 structs, constants, helpers, tests, and command paths.
- Use `rg` to check for stale references to `ComponentCodec`, `registry_version`, `cook_scene`,
  `dump_cooked`, `migrate_scene`, and short component-key fixtures.
- Keep only explicit unsupported cooked-scene stubs if needed for a temporary API boundary, with
  Slice 7 retirement noted in the main plan.

**Acceptance:**

- There is one authoritative component-key system for JSON v2: namespaced registry keys.
- Runtime and tool loading reject v1 JSON.
- No first-party tests preserve cooked v1 behavior.

## Validation

Chunk-local useful checks:

```bash
cmake --build --preset Debug --target teng_engine_tests engine_scene_smoke teng-scene-tool
./build/Debug/bin/teng_engine_tests
./build/Debug/bin/engine_scene_smoke
./build/Debug/bin/teng-scene-tool validate resources/scenes/demo_cube.tscene.json
```

Integrated slice exit:

```bash
./scripts/agent_verify.sh
```
