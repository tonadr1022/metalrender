# Phase 8: Library split and shipped-runtime linkage — implementation spec

**Status:** Phase 8 P0/P1 implementation landed: `metalrender` links static `teng_runtime`, the broad shared `teng` aggregate was removed, and the duplicate shared object lanes were collapsed.

**Scope:** CMake topology, linkage shapes per product (game runtime vs tests vs tools), Flecs single-runtime invariants, and optional promotion of internal buckets to first-class static libraries. **Out of scope for this document:** editor UI (Phase 9), full serialization v2 (Phase 12), RHI/renderer refactors.

**Expectation:** Large CMake and possibly minor source edits are allowed; internal target names may change; **`metalrender` default linkage is static ECS + core** per long-term shipped-runtime rule.

---

## 1. Problem statement

### 1.1 Why Phase 8 is “deep”

Today the repo already uses **four OBJECT buckets** (`teng_core`, `teng_platform`, `teng_gfx`, `teng_engine`) duplicated in shared-vs-static flavors, then welded into:

| Artifact | Role before Phase 8 |
|----------|------------|
| `libteng.so` (SHARED) | Full engine aggregate consumed by **`metalrender`** |
| `libteng_static.a` (STATIC) | Same logical library for **`teng_engine_smoke`** → `engine_scene_smoke` |

That means:

1. **Shipped-runtime rule is not met:** the primary demo/game-style exe links a **shared** aggregate while the architecture docs require **static** ECS + core for a player build ([`library_linkage_architecture_plan.md`](library_linkage_architecture_plan.md) § Long-term requirement).

2. **Shared + static Flecs rule is fragile:** only one consumer (`metalrender`) links shared `teng` today, but the pattern encourages future **multiple linkage units** pulling Flecs differently—duplicate `ecs_*` symbols or ODR violations if miswired.

3. **Boundaries are CMake-internal only:** `TENG_ENGINE_SOURCES` mixes **assets**, **Flecs scene**, and **render service** in one bucket. That is acceptable as an interim fold, but Phase 8 is the right time to **make dependency direction explicit** so Phase 9 (editor) and Phase 12 (GPU-free validate) can link **subsets** without accidentally pulling window/device/GPU.

4. **`TENG_BUILD_SHARED` is defined but unused** in first-party sources (only set in `src/CMakeLists.txt`). There is no export macro layer—good for a static flip, but any future **narrow shared ABI** (editor hot-reload) will need a deliberate design.

### 1.2 What is already solid

- **Single Flecs static archive** linked privately into aggregates (`flecs::flecs_static` on engine objects / aggregate)—no duplicate Flecs link lines in apps today.
- **GPU-free tool precedent:** `teng-shaderc` links only `teng_shader_compiler` + warnings—not full engine.
- **Smoke tests** now use `teng_runtime` via `teng_engine_smoke` ([`tests/CMakeLists.txt`](../tests/CMakeLists.txt)).

---

## 2. Goals and non-goals

### 2.1 Goals (must-have for Phase 8 exit)

1. **`metalrender` (shipped-style runtime)** links **static** engine + scene + Flecs in one player-like linkage unit—**no dependency on `libteng.so`** for that target (rename targets if desired).
2. **Exactly one Flecs runtime** per process—invariant preserved and **verifiable** after changes (see §7).
3. **`./scripts/agent_verify.sh`** and documented smokes keep passing with updated targets.
4. **Dependency DAG documented and enforced in CMake** at least at the level of “who may link whom” (editor/tools/game), matching the intent of [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) § Build targets.
5. **Clear retirement criteria** for interim shared `teng` if removed or demoted (§5).

### 2.2 Stretch goals (big refactors allowed; prioritize after must-haves)

1. **Promote OBJECT buckets to real STATIC libraries** (`add_library(... STATIC)`), possibly merging duplicate shared/static object twins into **one** object library consumed by two aggregates—reduces compile duplication and simplifies mental model.
2. **Split `teng_engine` source lists** into narrower CMake targets, e.g.:
   - **`teng_assets_runtime`** — `AssetDatabase`, `AssetRegistry`, `AssetService`, IDs/helpers that are GPU-agnostic.
   - **`teng_scene`** — `Scene`, `SceneManager`, `SceneComponents`, `SceneIds`, `SceneAssetLoader` (still interim TOML), Flecs registration paths.
   - **`teng_engine_runtime`** — `Engine`, layers, `RenderService`, extractors, engine-owned frame orchestration—depends on scene + gfx as today.

   Names are illustrative; the point is **one-way edges**: tools can depend on `teng_scene` + serialization later **without** `RenderGraph`.

3. **Introduce a dedicated GPU-free static target** for future CLIs (scene validate / migrate) even if no exe ships in Phase 8—can be `OBJECT`/`INTERFACE` library that fails to link if someone adds `VulkanDevice.cpp` upstream.

### 2.3 Non-goals

- Implementing editor binary or `EditorLayer` (Phase 9).
- Implementing reflection/codegen serialization (Phase 12).
- Changing runtime behavior of `Engine::tick()`, extraction, or meshlet rendering beyond what linkage forces (should be none).

---

## 3. Target architecture

### 3.1 Conceptual DAG (destination intent)

Aligns with the migration plan diagram; refine names when implementing:

```text
                    ┌─────────────────┐
                    │  metalrender    │  (STATIC-linked runtime)
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
       teng_engine_*   teng_gfx_*    teng_platform_*
              │              │              │
              └──────┬───────┴──────┬────────┘
                     ▼              ▼
                teng_core_*    flecs::flecs_static (once)
```

**Rules:**

- **Game/editor shared code** lives in static libs; differences are **which aggregate** and **which optional libs** (future `teng_editor_*`).
- **Tools** attach only to left subtrees (core + assets + scene + serialization) as needed.

### 3.2 Concrete linkage matrix (deliverable)

Produce a table in-repo (this spec or CMake comments) listing **every executable** and **PUBLIC/PRIVATE** deps:

| Exe / lib | Phase 8 target linkage |
|-----------|-------------------------|
| `metalrender` | Static aggregate (full runtime) |
| `engine_scene_smoke` | Static (subset or full static aggregate + smoke objs) |
| `teng-shaderc` | Minimal compiler static lib only |

Update **`AGENTS.md`** target section only when CMake target names change (optional rename).

### 3.3 Fate of shared `libteng`

**Decision branch (record the choice in the PR implementing Phase 8):**

- **Option A — Remove shared `teng`:** Simplest; matches “player static only” until an editor needs a shared slice. All consumers use `teng_static` or renamed static aggregate.
- **Option B — Keep shared `teng` for non-shipped experiments:** e.g. faster incremental linking during dev **only if** no second Flecs consumer appears. Must document **forbidden** pairings (never link shared `teng` + `teng_static` into one process).
- **Option C — Narrow shared ABI later:** Not Phase 8 unless explicitly scoped—would require export macros and stable C API slice.

**Recommendation:** Option A unless you already have a concrete shared-lib consumer scheduled within weeks.

---

## 4. Current code map (implementation anchors)

Relevant CMake and consumers:

- Engine aggregate: [`src/CMakeLists.txt`](../src/CMakeLists.txt) — `TENG_*_SOURCES`, `add_teng_component`, `teng_runtime` STATIC.
- App: [`apps/metalrender/CMakeLists.txt`](../apps/metalrender/CMakeLists.txt) — `target_link_libraries(metalrender PRIVATE teng_runtime)`.
- Smokes: [`tests/CMakeLists.txt`](../tests/CMakeLists.txt), [`apps/engine_scene_smoke/CMakeLists.txt`](../apps/engine_scene_smoke/CMakeLists.txt).
- Verify script: [`scripts/agent_verify.sh`](../scripts/agent_verify.sh) — builds `metalrender`, `teng-shaderc`, `engine_scene_smoke`.

No other CMake consumers of shared `teng` were found at spec time—**low blast radius** for flipping `metalrender` to static.

---

## 5. Phased implementation sequence

Workstreams can overlap; order minimizes breakage.

### Workstream A — Shipped-runtime linkage flip (P0)

1. Change `metalrender` to link **`teng_static`** (or a renamed alias such as `teng_runtime` that is **STATIC** and aggregates the same objects).
2. Fix **transitivity:** today `PUBLIC teng` may expose includes/libs to dependents of `metalrender`—there are none, but prefer **`PRIVATE`** linkage from exe to static aggregate unless a deliberate plugin boundary exists.
3. Full clean rebuild and run:
   - `./scripts/agent_verify.sh`
   - `./build/Debug/bin/metalrender --quit-after-frames 30`
   - `./build/Debug/bin/metalrender --scene resources/scenes/demo_cube.tscene.toml --quit-after-frames 30`
4. Confirm **no `libteng.so`** is loaded at runtime on Linux (`ldd` on `metalrender`)—expect only OS + driver stacks.

**Acceptance:** Phase 8 exit bullet “static ECS + core” satisfied for default game exe.

### Workstream B — Shared library retirement or quarantine (P0/P1)

If Option A:

1. Remove `add_library(teng SHARED ...)`, `*_shared` OBJECT twins **only if** nothing references them—otherwise keep one shared target unused and mark `DEPRECATED` in CMake message, then delete in a follow-up commit.
2. Collapse `add_teng_component(..., 1)` / shared object lanes if fully unused—**large CMake deletion**; validate compile time and that Metal custom targets still attach to remaining object libs.

If keeping shared temporarily:

1. Add `cmake_warning` / `message(WARNING)` when `BUILD_SHARED_TENG` is on.
2. Document forbidden link pairs in [`library_linkage_architecture_plan.md`](library_linkage_architecture_plan.md).

### Workstream C — OBJECT → STATIC promotion (P1, optional refactor)

Motivation: clearer IDE/clangd targets, faster linking granularity, cleaner `PUBLIC`/`PRIVATE` interface propagation.

1. Replace duplicated `_shared` / `_static` OBJECT libraries with **`add_library(teng_core OBJECT ...)`** once, then:
   - `add_library(teng_runtime STATIC $<TARGET_OBJECTS:teng_core> ...)` OR link OBJECT libs directly into static aggregate (CMake 3.24+ patterns)—pick one consistent pattern.
2. Re-establish **include directories** and **compile definitions** on each promoted target so `compile_commands.json` stays accurate for tidy.

**Acceptance:** No behavior change; same symbols in final exe; tidy/smokes green.

### Workstream D — Source-list splits (`engine` → assets / scene / runtime) (P2)

1. Inventory `#include` edges from each `.cpp` in `TENG_ENGINE_SOURCES` to classify files into proposed targets (§2.2).
2. Introduce CMake targets **before** moving files on disk if needed—start by splitting **lists only**.
3. Resolve circular includes if exposed (likely minimal within engine folder).
4. Ensure **no Flecs** leaks into a hypothetical **`teng_assets_runtime`** if split incorrectly—`AssetService` may reference scene types; adjust boundaries if cycles appear.

**Acceptance:** DAG drawn in CMake; `engine_scene_smoke` links strictly scene+assets+core without gfx **only if** smoke sources are refactored accordingly—otherwise intermediate step may still pull full static aggregate.

### Workstream E — Tooling hooks for Phase 12 (P2, scaffolding)

1. Add **`add_library(teng_scene_validate INTERFACE)`** or empty STATIC target with **link guards** comments—placeholder for GPU-free validate CLI.
2. No executable required in Phase 8 unless trivial.

### Workstream F — Documentation pass (P1)

1. Update [`library_linkage_architecture_plan.md`](library_linkage_architecture_plan.md) “Current layout” table to post-Phase-8 reality.
2. Update [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) Phase 8 **Completed** checklist when done (maintainer edit).

---

## 6. Migration scaffolding and retirement criteria

| Scaffolding | Role today | Retire when |
|-------------|------------|-------------|
| Shared `teng` + dual OBJECT lanes | Historical dev flexibility | All exes use static aggregate **and** no planned shared consumer, or shared replaced by narrow `teng_editor_api` |
| `teng_static` name | Smokes + static aggregate | Renamed for clarity (`teng_runtime`) once stable |
| `PUBLIC` exe→lib linkage on `metalrender` | Likely unnecessary | `PRIVATE` once confirmed no transitive consumers |

---

## 7. Risks, mitigations, and verification

### 7.1 Duplicate Flecs / ODR

**Risk:** Accidentally linking `flecs_static` twice through different paths or mixing shared Flecs with static.

**Mitigation:**

- Single CMake function **`teng_link_flecs(target)`** that adds `flecs::flecs_static` **PRIVATE** in one place.
- After build, optional script: `nm -C metalrender | rg '\becs_' | sort | uniq -c` — investigate duplicates beyond inline/static locals.

### 7.2 Link time and binary size

**Risk:** Static exe grows vs shared; link time increases.

**Mitigation:** Measure before/after (`time ninja metalrender`, binary size). Consider **LLD**, **thin archives**, or unity splits **later**—not a Phase 8 blocker.

### 7.3 Platform matrix

**Risk:** macOS framework linking behaves differently when static.

**Mitigation:** Build Metal preset in CI or manual maintainer check; verify `metalrender` still finds Metal/Vulkan backends per [`AGENTS.md`](../AGENTS.md).

### 7.4 Future editor DLL

**Risk:** Premature removal of shared lib blocks editor hot-reload.

**Mitigation:** Git history preserves shared CMake; Phase 9 plan explicitly chooses shared vs static editor **before** coding—this spec’s Option C path.

---

## 8. Validation strategy (required)

| Step | Command / check |
|------|-----------------|
| Configure + build + smoke | `./scripts/agent_verify.sh` |
| Bounded runtime | `./build/Debug/bin/metalrender --quit-after-frames 30` |
| Scene path | `./build/Debug/bin/metalrender --scene resources/scenes/demo_cube.tscene.toml --quit-after-frames 30` |
| Shaders (ifshader-related CMake touched) | `./build/Debug/bin/teng-shaderc --all` |
| Linkage audit | `ldd build/Debug/bin/metalrender` — **no** `libteng.so` |
| Optional symbol audit | `nm` / `llvm-nm` duplicate check for `ecs_` |

---

## 9. Relationship to adjacent phases

| Phase | Interaction |
|-------|-------------|
| **Phase 9 (editor)** | Separate exe adds **second consumer** of static libs—clean DAG from Phase 8 avoids pulling GPU into editor-only tools. |
| **Phase 12 (serialization)** | GPU-free validate CLI naturally links **`teng_scene` + assets** only if Workstream D landed; otherwise full static still works. |
| **Phase 10 (2D)** | Unaffected by linkage; continue to extend `RenderScene` / `IRenderer`. |

---

## 10. Open questions (resolve during implementation)

1. **Rename `teng_static` → `teng_runtime`** for clarity—worth churn?
Probably yes
2. **Should `engine_scene_smoke` link a reduced stack** after splits, or stay on full aggregate for simplicity?
3. **Windows MSVC future:** any need for `/WHOLEARCHIVE` or Flecs-specific link flags when static—defer until Windows port actively maintained?
4. **Install rules:** should `cmake --install` export **static** libs for external samples—out of scope unless requested?

---

## 11. Suggested Order

1. Functional; `metalrender` → static aggregate; verify `ldd`; keep shared target if risky.
2. Cleanup: Remove shared OBJECT lanes / shared lib; CMake dedupe.
3. Structure: Promote STATIC targets + engine source-list splits.
---

## 12. Exit criteria checklist (Phase 8)

Copy into PR description / tracking issue:

- [x] Default **`metalrender`** uses **static** linkage for core + ECS + engine (no `libteng.so` runtime dependency).
- [x] **Single Flecs** link model documented and audited.
- [x] **`agent_verify.sh`** green; bounded **`metalrender`** smokes green.
- [x] **`library_linkage_architecture_plan.md`** snapshot updated.
- [x] Shared `teng` **removed** or **explicitly quarantined** with forbidden-use notes.
- [x] Optional: OBJECT/STATIC promotion and/or engine CMake splits merged or ticketed as Phase 8.1.

---
