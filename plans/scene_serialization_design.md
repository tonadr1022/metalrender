# Phase 12 — Scene serialization, cook, and validation (implementation specification)

**Parent roadmap:** [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) — Phase 12.  
**Related:** [`asset_registry_implementation_plan.md`](asset_registry_implementation_plan.md) (cooked manifests, `AssetId` closure), [`library_linkage_architecture_plan.md`](library_linkage_architecture_plan.md) (GPU-free tool linkage).

---

## 0. Purpose and authority

This document is sufficient for an agent to **implement** Phase 12 without guessing product intent. Where tradeoffs exist, it states **decision criteria** and **recommended defaults**; the implementing agent should **record any remaining optional choices** in code comments or a one-line note in this file’s “Resolved decisions” section at the end of the work.

**Backward compatibility:** **Not required.** Interim `schema_version = 1` text scenes loaded by `SceneAssetLoader` may be **deleted**, **replaced**, or **bulk-migrated** to canonical JSON in one pass. Demo assets and `scripts/generate_demo_scene_assets.py` may change wholesale. Large diffs (thousands of lines) are acceptable if they reduce long-term surface area.

**Scope boundary:** Phase 12 is **scene and authored content** serialization and cook. **Player progression / save games** remain out of scope (see migration plan [Scope honesty](engine_runtime_migration_plan.md#scope-honesty-not-designed-yet)).

**Out of scope for this spec:** Application **project** configuration files (e.g. `resources/project.toml` keys such as `startup_scene`) may stay TOML for human-edited project settings; those files only **reference paths** to scene assets. **Canonical scene file bytes** and **all Phase 12 scene serialization code** are **JSON-only** as specified below — **not** TOML and **not** `toml++`.

---

## 1. North-star outcomes (definition of done)

An agent can consider Phase 12 **complete** when all of the following are true:

1. **Single source of truth** — Every **authored** component type that must appear in shipped scenes is registered in one **component serialization registry**. Adding a new serializable component does **not** require hand-editing a parallel loader (contrast: today’s `SceneAssetLoader.cpp` vs `SceneComponents.hpp` drift).

2. **Round-trip** — For the set of **serialized authored components** (§4), `deserialize(serialize(scene))` is **semantically equivalent** to the original for extraction and gameplay: same `EntityGuid` sets, same payloads for every serialized component, stable entity ordering (§8). **Excluded from byte-for-byte component comparison:** runtime-only components (§4), **`LocalToWorld`** (derived after load per §5.3), and any other registry-flagged derived/cache rows. After round-trip, **`LocalToWorld`** must match **deterministic derivation** from authored `Transform` (and FPS/controller rules in code) before gameplay reads—not necessarily equality with pre-save runtime matrices.

3. **Runtime load path** — `Engine` / `SceneManager` loads scenes through the **new** pipeline (registry-driven). The interim `SceneAssetLoader` API is **removed**.

4. **Authoring format** — **Canonical scene files** are **UTF-8 JSON** (§7.1–§7.2), human-mergeable when pretty-printed, diff-friendly (stable key ordering policy on save), full **read + write** from engine and GPU-free tools, **documented** here and covered by tests.

5. **Cooked binary** — A **cooked** on-disk representation exists that is suitable for **shipped** or **CI** runtime loading without parsing heavy text, with an explicit **binary format version** (§7.3, §3) and a **cook** entry point (library API + CLI).

6. **GPU-free validation** — Executable **`teng-scene-tool`** (Phase 12; links **`teng_scene_tool_lib`**) can **validate** and **migrate** scene files **without** linking `teng_gfx`, window, or `RenderService` (§10–§11). `./scripts/agent_verify.sh` builds this executable so the toolchain does not silently rot.

7. **Repo gates** — `./scripts/agent_verify.sh` passes; `metalrender` smokes in `AGENTS.md` pass with updated demo/project paths; smoke tests that referenced the removed `SceneAssetLoader` are updated to the new API.

---

## 2. Current repository inventory (do not guess — read these)

| Area | Role | Key paths |
|------|------|-----------|
| Scene ECS host | `Scene`, `SceneManager`, Flecs world | `src/engine/scene/Scene.hpp`, `Scene.cpp`, `SceneManager.*` |
| Components | C++ structs registered on the world | `src/engine/scene/SceneComponents.hpp` |
| Stable IDs | `SceneId`, `EntityGuid`, `AssetId` | `src/engine/scene/SceneIds.*` — `EntityGuid` is **`uint64_t value`** with **`operator<`** = unsigned numeric order on `value` |
| Interim load-only text loader | Legacy subset of components (to be removed) | `src/engine/scene/SceneAssetLoader.*` |
| Extraction | ECS → `RenderScene` | `src/engine/render/RenderSceneExtractor.cpp`, `RenderScene.hpp` |
| App entry | `--scene`, `project.toml` startup paths | `apps/metalrender/main.cpp`, `resources/project.toml` |
| Demo generation | Writes legacy scene files + registry (migrate to JSON) | `scripts/generate_demo_scene_assets.py` |
| GPU-free tool library | Static lib (validate/migrate/cook implementation grows here) | **`teng_scene_tool_lib`** → `src/engine/scene/SceneValidate.cpp` (expand beyond stub as needed) |
| Scene CLI | Thin executable, subcommands | **`teng-scene-tool`** → `apps/teng-scene-tool/` |
| Smoke | Loader + assets | `tests/smoke/SceneAssetLoaderSmokeTest.cpp`, `GeneratedSceneAssetsSmokeTest.cpp`, `apps/engine_scene_smoke` |

**Known drift (must be fixed by design, not ad hoc):**

- `SceneAssetLoader` does **not** load `SpriteRenderable`, `FpsCameraController`, or other components present in `Scene::register_components()`.
- `EngineInputSnapshot` is a **singleton-style** world component for input; it must **not** be serialized as per-entity state (§5.2).
- **`LocalToWorld` is never authored on disk** (§5.3); it is rebuilt after load.

---

## 3. Design principles (long-term)

1. **Serializable vs runtime-only vs derived** — Only **authored, stable** simulation/presentation inputs belong in canonical scene files. Runtime caches, input snapshots, FPS controller state, and **derived transforms (`LocalToWorld`)** are **not** serialized.

2. **Stable identities** — On disk, references use **`EntityGuid`**, **`AssetId`**, and **`SceneId`** semantics already defined in `SceneIds.hpp`. No filesystem paths in authored scenes. No GPU handles, no Flecs entity ids in files.

3. **Schema evolution (canonical rule)** — **`registry_version`** is the **single authoritative monotonic integer** for the **canonical JSON envelope + every registered component payload codec** shipped together. When **any** breaking change occurs (new required field, incompatible payload shape, envelope key changes), bump **`registry_version`** and extend the registry **`migrate`** pipeline. **Do not** rely on independent per-component version fields in v1 files.

   **Binary cooked format:** The cooked blob header carries its own **`binary_format_version`** (layout of tables, alignment, padding—§7.3). It is **not** the same integer as **`registry_version`**. The cook implementation documents which **`registry_version`** ranges each **`binary_format_version`** can encode. Reject unknown pairs with a clear error.

4. **One registry** — Serialization metadata drives **load/save**, future **inspector** (Phase 9+), and **validation** messages (§8.1). **Normative validation** is **registry-driven**: known top-level keys, required envelope fields, registered component keys, and typed payload checks—**not** a separate JSON Schema document unless the project adds one later for ancillary tooling.

5. **Presentation ≠ serialized gameplay** — Do not serialize `RenderScene`, GPU batching, or meshlet data. Keep the boundary used today: ECS components → extraction → render.

6. **Tooling linkage** — Cook/validate CLIs link **minimal** static libs (`teng_scene`, `teng_assets`, `teng_core`, **`teng_scene_tool_lib`**, …) per [`library_linkage_architecture_plan.md`](library_linkage_architecture_plan.md); do not pull the full renderer for file I/O.

---

## 4. Serializable component set (v1 of Phase 12)

### 4.1 Serialized authored components (minimum milestone)

**Minimum** set for the first mergeable milestone (prove registry + round-trip on real data):

| Component | Serialize | Notes |
|-----------|-----------|--------|
| `EntityGuidComponent` | Implicit from entity record | Always present for every stored entity |
| `Transform` | Yes | §5.5: parent-local with implicit root until hierarchy lands |
| `Camera` | Yes | |
| `DirectionalLight` | Yes | |
| `MeshRenderable` | Yes | `AssetId` only |
| `SpriteRenderable` | Yes | `AssetId` + tint + sorting fields |

**Human-readable entity label:** Optional **top-level** **`name`** string on each entity record (§5.2). **Do not** serialize a **`Name`** component under **`components`**—single source of truth on disk is the entity envelope field only. Runtime may still attach a Flecs `Name` (or equivalent) **derived from** that string when loading; saves **must** emit **`name`** from the canonical runtime source for that label (not a divergent second field).

### 4.2 Derived at runtime (never in canonical JSON)

| State | Reason |
|-------|--------|
| `LocalToWorld` | Deterministic function of `Transform`, hierarchy (when present), and FPS/camera systems—§5.3 |

### 4.3 Explicitly runtime-only (do not serialize as entity components)

| Component / state | Reason |
|-------------------|--------|
| `EngineInputSnapshot` | Frame input; not authored |
| `FpsCameraController` | Runtime simulation state; **not** authored on disk. Editor yaw/pitch defaults are **not** serialized in v1; reintroduce later only via an explicit authoring component if needed |

If a component is added to `SceneComponents.hpp` for gameplay, the registry must gain an entry **in the same change** or the change must be labeled **non-serialized experimental** with a tracked follow-up.

---

## 5. Serialization domain model

### 5.1 Component type identity

Each serializable component type needs a **stable string key** used in canonical files (e.g. `"transform"`, `"mesh_renderable"`). Keys:

- Are **lower_snake** and stable across C++ renames unless intentionally versioned.
- Map to C++ types via the registry (not scattered `if (key == ...)` in one god file).

Optional future: hash keys in **binary** format only; canonical text keeps readable names.

### 5.2 Entity record

Each entity in a scene file:

- Has **`guid`**: JSON **number** when the value is in the IEEE-754 safe integer range (§7.4), encoding **`EntityGuid::value`** as **`uint64_t`** consistent with `SceneIds.hpp`.
- Has optional **`name`** (string) — **only** place the entity’s authored label appears on disk (§4.1).
- Has an object **`components`**: map **component key → payload object** for **serialized authored** components present on that entity (**no** `local_to_world`, **no** `name` component payload).
- **`entities` array** order is **sorted by `EntityGuid::value` ascending as unsigned `uint64_t`**, matching `operator<(EntityGuid, EntityGuid)` in `SceneIds.hpp`. Apply the **same** rule in **save** and **cook** text staging.

**Illustrative JSON shape (not normative field-by-field):**

```json
{
  "registry_version": 1,
  "scene": { "name": "demo" },
  "entities": [
    {
      "guid": 10001,
      "name": "camera",
      "components": {
        "transform": { "translation": [0, 0, 3], "rotation": [1, 0, 0, 0], "scale": [1, 1, 1] },
        "camera": { "fov_y": 1.047, "z_near": 0.1, "z_far": 10000.0, "primary": true }
      }
    }
  ]
}
```

### 5.3 `LocalToWorld` policy

**Normative:**

- Canonical JSON **never** contains `LocalToWorld`. Validators **error** if an unknown or forbidden key **`local_to_world`** appears under **`components`** (strict mode; §7.6).
- On **load**, after applying authored components (at minimum `Transform`), **derive** `LocalToWorld` **deterministically without requiring `tick`**—same rules as today’s non-FPS path (`transform_to_matrix` / systems ordering equivalent) and FPS/camera paths coded in the scene module. **Save** never reads `LocalToWorld` for serialization.
- **Round-trip tests** assert equality on **serialized** components and guid sets; they assert **`LocalToWorld`** matches **expected derived matrices** from authored state after load, not bitwise preservation from before save.

### 5.4 Scene-level metadata

Persist at least:

- **Scene name** (string) — maps to `Scene` construction / rename.
- **`registry_version`** — **required** integer at top level; authoritative evolution marker (§3).
- Optional: **source scene id**, **authoring tool id**, **dependencies** list (`AssetId` list or manifest reference) for cook closure alignment with [`asset_registry_implementation_plan.md`](asset_registry_implementation_plan.md).

### 5.5 Transform without hierarchy (pre–parent/child phase)

Until parent/child appears in ECS and the format (§15), **`Transform` is interpreted as parent-local** with an **implicit root**: every entity behaves as if parented to the world root, so **parent-local equals world-space** for positioning. Document this in tooling UI so demos and editor expectations stay honest. When hierarchy lands, **`Transform` remains parent-local**; semantics gain real parent chains without redefining the meaning of the component.

---

## 6. Component registry architecture

### 6.1 Responsibilities

The registry must support:

- **Register** each C++ type with: type key, (de)serialize functions to/from **`nlohmann::json`** payload objects (or equivalent typed extraction), optional **`migrate(from_registry_version, json)`** from older payload shapes, **flags** (strip on cook, etc.—no “editor-only serialization” split for `FpsCameraController` in v1; it is not serialized).
- **Enumerate** serializable types for reflection/inspector and for **unknown key** diagnostics in files.
- **Serialize entity** — given `flecs::entity`, write **only** authored component blobs to the canonical model; **omit** `LocalToWorld` and runtime-only components.
- **Deserialize entity** — given blobs, add components to a `Scene` / `flecs::entity` in a defined order (parents/children deferred to future hierarchy phase if not in ECS yet).

### 6.2 Implementation strategies (pick one; hybrid allowed)

| Approach | Pros | Cons |
|----------|------|------|
| **Manual registration table** in one TU | Simple, explicit, no codegen | Verbose; easy to forget a field |
| **Macro-generated registration** | Less boilerplate | Magic; needs discipline |
| **Codegen** from a **schema** file (JSON Schema / custom) | Single source for docs + code | Extra build step |
| **Flecs metadata** / reflection if adopted later | Unified with ECS | Up-front integration cost |

**Recommendation for this repo:** Start with a **central registration** in `src/engine/scene/` (e.g. `SceneSerializeRegistry.*`) with **explicit** per-component functions and **strong tests**. Refactor to codegen **after** the first round-trip ships if duplication hurts.

### 6.3 Error model

Use existing `Result` / `make_unexpected` patterns (`core/Result.hpp`). Validation errors should carry **file path**, **entity guid**, and **component key** where possible for CLI output.

For **`nlohmann::json`** parse failures: map diagnostics into `Result` with **both** (when available) **byte offset** into the UTF-8 input buffer **and** **character / logical position** as reported by the library (for non-ASCII paths these may diverge—do not label character indices as byte offsets). Prefer quoting a **short UTF-8 snippet** around the failure when helpful.

---

## 7. On-disk formats

### 7.1 Canonical authoring format (JSON, mandatory)

**Requirement:** Canonical scene files are **JSON** ([RFC 8259](https://www.rfc-editor.org/rfc/rfc8259)), **UTF-8**, human-mergeable when pretty-printed, deterministic **save** output (indentation + **sorted object keys** at each object level unless a documented exception), round-trip stable.

**File extension:** **`*.tscene.json`** (recommended) or a single agreed extension documented in `AGENTS.md`.

**Forbidden for canonical scenes:** TOML, YAML as primary authoring, or **`toml++`** in scene serialization implementation. Do not introduce a second first-class text scene format.

**Optional import-only:** A one-off importer from legacy formats may exist only if clearly labeled **import-only** and not required for editor save or CI round-trip.

### 7.2 JSON library: **nlohmann/json** (mandatory)

Use **[nlohmann/json](https://github.com/nlohmann/json)** as the **only** JSON implementation for Phase 12 scene parse/emit and registry payload manipulation.

**Why this library (long-term fit):**

- **Round-trip and ergonomics:** Natural mapping between JSON values and structured component payloads; straightforward integration with `Result`-based validation.
- **Maintenance and ecosystem:** Widely used, stable CMake target **`nlohmann_json::nlohmann_json`**, permissive license (MIT), active releases — suitable for engine and GPU-free tools for years without bespoke parser ownership.
- **Diagnostics:** Parse errors expose position/context; combine with file paths for CLI tools (`validate`, `migrate`).
- **Performance:** Adequate for authored scene sizes and tool pipelines; **hot loading** is addressed by **cooked binary** (§7.3), not by squeezing text parse latency.

**Integration:**

- Add **`nlohmann_json`** to CMake for targets that implement scene file I/O (at minimum `teng_scene` or a dedicated `teng_scene_serialize` static library and scene tool executables). Prefer pinning a **released tag** via FetchContent, existing package manager, or vendored copy consistent with repo policy — record the chosen mechanism in `cmake/README.md` or equivalent when implemented.
- Do **not** substitute alternate JSON libraries for scene serialization unless this spec is explicitly revised (single implementation reduces audit surface and linker complexity).

**Out of scope for v1:** JSON5, JSONC, or trailing commas — standard JSON only unless a future phase extends the spec.

### 7.3 Cooked binary format

**Goals:** Fast load, small size, mmap-friendly **optional** (future), explicit versioning, **no** GPU objects.

**Contents (minimum):**

- **Header:** magic bytes, **`binary_format_version`**, `flags`, offsets to tables.
- **Embedded `registry_version`** (or equivalent) so loaders reject incompatible semantic payloads even if the binary container matches.
- **String table** (interned component keys, entity names).
- **Asset table** — list of `AssetId` values referenced (supports closure validation against cooked asset manifest per asset registry plan).
- **Entity table** — fixed-size records: `guid`, indices into string/asset tables, bitmask or tag list of present components.
- **Component blob section** — typed binary payloads matching registry binary codec.

**Endianness:** **Little-endian only.** Supported engineering platforms are little-endian; **big-endian hosts are unsupported** for reading/writing cooked blobs—reject or fail fast with a clear error if detected.

**Alignment / packing:** Explicit rules **must** be written into the format header comment and implementation **before** cooked golden-byte tests are enabled (§12.1). Until then, treat binary layout as **implementation-detail** revocable by **`binary_format_version`** bumps.

### 7.4 JSON numeric notes, `guid`, and ordering

- **`registry_version`** as a JSON integer (required top-level).
- **`guid`** values must round-trip **`EntityGuid::value`** / `uint64_t` **exactly**.

**Policy (aligned with current code generators):** `make_entity_guid()` allocates monotonic **`uint64_t`** values from 1 upward (`SceneIds.cpp`), which stay within JSON’s safe integer range (**≤ 2^53 − 1**) for the foreseeable future. **Canonical JSON therefore uses JSON numbers for `guid`** for typical scenes. Validators **must** reject **`guid`** values that are not integers in safe range **or** that fail to parse as the intended unsigned 64-bit value.

If a future pipeline requires arbitrary full-range **`uint64_t`**, introduce an opt-in **string** encoding **only** with an explicit **`registry_version`** bump and migrator; **sort order remains unsigned numeric order of `EntityGuid::value`**, i.e. parse decimal string to **`uint64_t`** and compare—**never** rely on lexicographic string sort for entity ordering.

**Top-level keys (normative envelope; component payloads remain registry-defined):**

```text
registry_version: number   # required; §3
scene: { name: string, ... }
entities: [ { guid, name?, components: { "<component_key>": { ... } } }, ... ]
```

### 7.5 Forward compatibility and strictness (defaults)

Unknown keys (top-level, under `scene`, under an entity, or under `components`) are diagnosed by comparing against the **registry** and fixed envelope allowlists.

**Defaults (normative):**

| Surface | Unknown / forbidden keys |
|---------|---------------------------|
| **`teng-scene-tool validate`** | **Error** (non-zero exit). No silent skip. Optional **`--warn-unknown`** flag may downgrade to warning-only for exploratory local use; **CI uses strict default**. |
| **Runtime game load** (`load_scene` / equivalent) | **Error** — reject the file. Shipped content is expected to match the built-in **`registry_version`** contract. |
| **Editor load/save** | **Error** by default on load for unknown keys (same as runtime). Optional editor-only **“compat preview”** mode may warn-and-skip **only** behind an explicit UI toggle or CLI—not the default. |

### 7.6 Cook pipeline

Provide:

- **Library API:** `cook_scene(canonical_path | parsed JSON) -> cooked_path` + **optional** in-memory buffer API for tests.
- **CLI:** **`teng-scene-tool cook`** — **must** link GPU-free libs only.

**Relationship to assets:** Cook step should optionally **emit** or **consume** a manifest snippet listing `AssetId` dependencies for the runtime registry loader (align with open work in `asset_registry_implementation_plan.md`).

---

## 8. Flecs load and save algorithms

### 8.1 Load (canonical JSON → `Scene`)

1. Read file → parse with **`nlohmann::json`** → validate **`registry_version`** (known set + migrate path), envelope keys, and **registry-driven** payload checks (**no separate JSON Schema artifact required** for Phase 12—§3).
2. `SceneManager::create_scene(name)` or clear existing — API decision: loading into **new** scene vs **in-place replace** of active world (default: **new scene**, set active like today).
3. For each entity record (**deterministic sort order**, §5.2):
   - `create_entity(guid, name)` using **top-level `name`** only.
   - For each component payload under `components`: **deserialize** via registry and `set` on entity.
4. **Do not** serialize `EngineInputSnapshot`; if needed, runtime sets it each frame as today.
5. After applying payloads, **derive `LocalToWorld`** deterministically (§5.3). Prefer **no** `tick(0)` requirement; if a future subsystem forces it, document that exception prominently.

### 8.2 Save (`Scene` → canonical JSON)

1. Iterate entities with `EntityGuidComponent` (all serializable entities).
2. For each entity, **skip** runtime-only and derived components per registry rules (`LocalToWorld`, `FpsCameraController`, `EngineInputSnapshot`, …).
3. Emit **top-level `name`** from the canonical runtime label source (§4.1); **do not** emit a `name` component blob.
4. Emit entities in **sorted `EntityGuid::value` order** (unsigned).
5. Write **`registry_version`** and metadata; **`dump`** with stable formatting (indent + sorted keys).

### 8.3 Interaction with systems

Systems registered in `Scene::register_systems()` run on **update**. **`LocalToWorld`** after load must be correct **before** first `tick` for static scenes; FPS controllers **overwrite** `LocalToWorld` each frame as they do today—**saving immediately after systems run** captures whichever transient state exists in **serialized** components only (`Transform`, etc.), never `LocalToWorld`.

---

## 9. Engine and application integration

### 9.1 Replace `load_scene_asset`

- Remove or replace `teng::engine::load_scene_asset` in `SceneAssetLoader.*` with **`load_scene_file`** (name TBD) implemented on top of the registry and JSON I/O.
- `Engine::load_scene_asset` in `Engine.cpp` should forward to the new implementation; **rename** the method to `load_scene` / `load_scene_from_file` if it reduces confusion.

### 9.2 CLI and project startup paths

- `apps/metalrender/main.cpp` **`--scene`**: accept canonical **`.tscene.json`** (or chosen extension) and load via the new pipeline.
- `resources/project.toml` **`startup_scene`**: update demo paths to JSON scene files. *(This file remains project configuration TOML; only the referenced scene paths change.)*

### 9.3 Demo generator

- Rewrite `scripts/generate_demo_scene_assets.py` to emit **canonical Phase 12 JSON** (same logical content as today’s demos, layout per §5.2 / §7).
- Keep **idempotent** behavior; align numeric / matrix conventions with the registry codec documentation.

---

## 10. GPU-free tools: validate, migrate, dump

### 10.1 Naming (CMake and binaries)

| Artifact | Role |
|----------|------|
| **`teng_scene_tool_lib`** | **STATIC** library — registry helpers, validate/migrate/cook implementation (GPU-free). |
| **`teng-scene-tool`** | **Executable** — subcommands only; links **`teng_scene_tool_lib`**, **`teng_scene`**, **`teng_assets`**, **`teng_core`**, **`nlohmann_json::nlohmann_json`** when JSON is wired. |

**`./scripts/agent_verify.sh`** should include **`teng-scene-tool`** in default **`TARGETS`** so CLI sources compile and link continuously (same rationale as the former standalone validate scaffold).

### 10.2 Commands (minimum)

| Command | Behavior |
|---------|----------|
| `validate <path>` | Parse JSON (`nlohmann/json`), resolve registry, run semantic checks (`registry_version`, AssetId parse, guid uniqueness, required components). Exit non-zero on error. |
| `migrate <in> <out>` | Bump **`registry_version`** / rewrite payloads using registered migrators. |
| `cook <in> <out>` | Write binary cooked scene (`binary_format_version` §7.3). |
| `dump <binary> <out>` | Optional: JSON dump for debugging (same canonical conventions). |

All must run **without** Vulkan/Metal window creation.

### 10.3 Linkage

Do **not** link `teng_render` / `teng_gfx` for these CLIs. If `AssetService` is needed for validation that resolves assets, prefer **optional** `--project-root` + registry DB scan — if that drags in engine runtime, split **pure ID validation** (always) from **deep asset existence** checks (optional flag).

---

## 11. CMake and library layout (allowed refactors)

Large refactors are **in scope**:

- Split **`teng_scene_serialize`** (STATIC) containing registry + canonical JSON + binary codecs, depended on by `teng_scene` and tools, **if** it clarifies dependencies. Alternatively, keep everything under `teng_scene` until file count forces a split.
- Ensure **`flecs`** remains only in `teng_scene` (per linkage plan).
- **`nlohmann_json`** must be available to every target that parses or emits canonical scene JSON (engine scene module and **`teng_scene_tool_lib`** / **`teng-scene-tool`**).
- Update `src/CMakeLists.txt` and `apps/CMakeLists.txt`; update `cmake/README.md` if target DAG or FetchContent pins change.

---

## 12. Testing and validation strategy

### 12.1 Unit / integration tests

- **Round-trip (JSON):** Build a `Scene` in memory with known **serialized** components → save JSON to temp → load → compare **guid sets** and **per-component equality** for authored types; verify **`LocalToWorld`** against **derived** expectations (§5.3).
- **Golden files:** Commit **canonical** demo **`.tscene.json`** files and hash or diff them in CI. **Cooked binary golden-byte or hash tests are deferred** until **`binary_format_version`** layout rules are fixed (§7.3); until then, rely on round-trip **JSON** and optional **cook → dump → JSON compare** without asserting raw blob hashes.

### 12.2 Smoke updates

- Replace `SceneAssetLoaderSmokeTest` with **`SceneSerializeSmokeTest`** (or rename file) covering validate + load + optional round-trip.
- `GeneratedSceneAssetsSmokeTest` continues to run after generator update.

### 12.3 `agent_verify.sh`

- Build **`teng-scene-tool`** by default.
- Optionally run **`teng-scene-tool validate`** on `resources/scenes/*` once canonical JSON exists (fast, GPU-free).

---

## 13. Phased implementation sequence (for agents)

Execute in order to keep main green. Each sub-phase should be merge-sized where possible, but **multi-thousand-line** refactors are acceptable when they delete old paths cleanly.

| Sub-phase | Goal | Exit signal |
|-----------|------|-------------|
| **12-A** | Registry + **one** component end-to-end (e.g. `Transform` + **top-level `name`** + guid) + JSON tests | Load minimal `.tscene.json` into `Scene` |
| **12-B** | Full **minimum** component set (§4) + round-trip tests | Demo scene loads through new path |
| **12-C** | Remove / retire `SceneAssetLoader`; update smokes, `main`, `project.toml` paths, generator | No duplicate loader maintenance |
| **12-D** | Binary cooked format + cook CLI + `metalrender` can load cooked **or** canonical (pick default for `--scene`: recommend **canonical JSON** for dev, **cooked** optional flag `--scene-cooked`) | Document in `AGENTS.md` if CLI changes |
| **12-E** | Flesh out **`teng-scene-tool`** subcommands + `agent_verify` validate hook | GPU-free CI coverage |

Parallel documentation: update **`AGENTS.md`** smoke examples if paths or flags change.

---

## 14. Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Registry and Flecs type registration diverge | Single header list or static assert linking macro to `Scene::register_components()` |
| Binary format churn | **`binary_format_version`** in header; old binaries rejected with clear error |
| Cook closure vs asset DB mismatch | Emit asset list from cook; integrate manifest when asset plan lands |
| Large refactor breaks linkage | Follow `library_linkage_architecture_plan.md`; run `./scripts/agent_verify.sh` frequently |
| `uint64` guid precision in JSON | Safe integer **`guid`** policy §7.4; migrator if string form is ever added |

---

## 15. Open decisions (resolve during implementation)

Record answers here when closed:

1. Default `metalrender --scene` file type: canonical JSON vs cooked binary.
2. Whether **entity hierarchy** (parent/child) appears in Phase 12 or a follow-up — if not in ECS yet, **omit** from v1 format (Transform semantics §5.5 until then).

---

## 16. Resolved decisions (maintenance)

_Update this subsection as the team locks choices during implementation._

- **Canonical scene text format:** JSON only (`*.tscene.json` recommended); **not** TOML for scene bytes.
- **JSON implementation:** **[nlohmann/json](https://github.com/nlohmann/json)** (`nlohmann_json::nlohmann_json`) for all Phase 12 scene parse/emit and registry JSON payloads; scene serialization **must not** use `toml++`.
- **`FpsCameraController`:** **Runtime-only** — not serialized in v1 (§4.3).
- **Entity label:** **Top-level `name` only** on disk — no `Name` component JSON blob (§4.1, §5.2).
- **`LocalToWorld`:** **Never on disk**; derive after load (§5.3); round-trip compares authored components + deterministic derivation.
- **Evolution:** **`registry_version`** monotonic authority for JSON + registry codecs; **`binary_format_version`** separate for cooked layout (§3, §7.3).
- **Validation:** **Registry-driven** envelope + payloads; no normative JSON Schema requirement for v1 (§3, §8.1).
- **Endianness:** Cooked blobs **little-endian**; **no big-endian support** (§7.3).
- **Golden tests:** **JSON-first**; cooked byte hashes wait on fixed packing rules (§12.1).
- **Tools:** **`teng_scene_tool_lib`** + **`teng-scene-tool`** exe (§10.1); **`agent_verify.sh`** builds the exe.
- **Pre-hierarchy `Transform`:** **Parent-local** with implicit world root (§5.5).
- **Parse diagnostics:** Report **byte offset and character/context position** when available (§6.3).
- **Forward-compat defaults:** **Strict error** on unknown keys for validate, runtime load, and editor default (§7.5).
