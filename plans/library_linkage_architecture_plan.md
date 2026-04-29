# Library linkage and split architecture plan

Status: **design note** — proposes long-term CMake/library boundaries for `metalrender`: what should ship as shared libraries, what should link statically into executables, and how to avoid repeating mistakes around third-party ECS linkage (see [`tests/CMakeLists.txt`](../tests/CMakeLists.txt) comment on Flecs + `libteng`).

Relationship: complements [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) (runtime behavior and layers); this document is **only** about **build/link topology** and **ABI surfaces**.

## Goals

- Define a **small number of intentional boundaries** (shared vs static) instead of one accidental monolith.
- Keep **exactly one Flecs runtime per process** without exporting Flecs symbols from DSOs or linking `flecs_static` twice into conflicting roles.
- Align **tools/tests** (fast iteration, boring links) with **shipping games** (controlled modules, platform constraints) without maintaining two unrelated CMake worlds.
- Preserve [`AGENTS.md`](../AGENTS.md) guardrails: `./scripts/agent_verify.sh`, `vktest --quit-after-frames 30`, shader compile flows.

## Non-goals

- Rewriting RHI, render graph, or renderer internals for linkage reasons alone.
- Choosing a packaging format for end users (Steam, console packages) beyond “what links how.”
- Mandating static-only or shared-only for every platform forever — the plan phases options.

## Current state (snapshot)

| Artifact | Kind | Role |
|----------|------|------|
| `teng` (`libteng.so` / `.dylib`) | **SHARED** | Large engine + gfx + assets + scene + flecs (private) + Vulkan/Metal pieces |
| `teng_shader_compiler` | **STATIC** | Shader compiler; linked into `teng` and `teng-shaderc` |
| `teng_engine_smoke` | **STATIC** | GPU-free smoke tests; **PUBLIC** link to `teng`; scene ECS smokes remain **inside** `teng` because Flecs is not re-exported from the shared library |
| `vktest`, `engine_scene_smoke`, `teng-shaderc` | **EXECUTABLE** | Apps link `teng` (and smoke lib where relevant) |

Pain points already observed:

- **Third-party static deps linked privately into a shared `teng`** do not expose their C/C++ ABI to **other** static libraries or TUs that include the same headers unless those TUs live inside `teng` or you duplicate the runtime (bad for ECS).
- Splitting “tests” into a separate static library works when tests call **only** stable engine APIs; it breaks when tests **inline** Flecs C++ API while Flecs lives only inside `libteng.so`.

## Principles (what “real” engines usually optimize for)

1. **One runtime per dependency class per process** — especially ECS, scripting VMs, and allocators that hold global or quasi-global state.
2. **Explicit ABI surfaces** — shared libraries expose **narrow, versioned** APIs (C or stable C++ subset); everything else stays inside the same linkage unit or behind opaque handles.
3. **Shipped games** often favor **few binaries** and **predictable symbols** (static monolith or a **small** set of DLLs/SOs with clear roles: render device, audio, platform).
4. **Tools and CI** favor **fast links** and **simple graphs**; duplicating large static libs across ten tiny tools can be acceptable on desktop dev machines.

These principles conflict slightly; the migration phases below pick defaults per **phase** and record **retirement criteria** when upgrading.

## Recommended long-term shape (target)

### A. Thin executable runtime (always static glue)

Every **game/editor/tool entry** should stay a **thin** `main` + argument parsing + perhaps crash hooks:

- **Statically linked**: bootstrap, memory/debug init, logging setup if not inside engine, and calls into the engine library.

This does **not** mean “statically link the entire engine into every exe” forever — it means the **executable itself** is not where large reusable code lives.

### B. Engine core vs presentation split (medium-term CMake targets)

Split **`teng`** into layered libraries with **one-way dependencies** (avoid cycles):

| Proposed target | Contents (conceptual) | Default linkage suggestion |
|-----------------|------------------------|----------------------------|
| `teng_core` | Types, logging, files, math helpers, **non-Flecs** or minimal ECS-neutral IDs if feasible | **STATIC** first; revisit **SHARED** only if multiple consumers need ABI stability |
| `teng_scene` (or `engine_scene`) | Flecs world, `Scene`, components, extraction that touches ECS | **Same linkage mode as the runtime that owns worlds** — typically **STATIC into** the main engine DLL **or** **STATIC into** final exe |
| `teng_render` / existing gfx cluster | `RenderService`, `RenderGraph`, RHI, meshlet path | Often **SHARED** “engine” slice for editor + game if both exist; otherwise **STATIC** into a single game binary |
| `teng_assets` | `AssetRegistry`, `AssetDatabase`, `AssetService`, load pipelines | **STATIC** into engine unless a standalone asset processor ships separately |
| `teng_shader_compiler` | Already separate static lib — **keep** |

Exact file lists are implementation details; the **rule** is: **Flecs-using code never sits in a separate static `.a` that links against a **SHARED** `teng` that **also** contains Flecs unless Flecs is guaranteed single-instance (see below).

### C. Flecs linkage rule (non-negotiable invariant)

Pick **one** model and document it in CMake:

1. **Encapsulated Flecs (preferred for shared `libteng`)**  
   - No `flecs` headers in **public** engine headers consumed by games/tests outside `teng`.  
   - Scene smoke tests that need Flecs live **inside** the same library as `Scene` **or** tests link a **fully static** engine stack (see D).

2. **Static engine for tests that include Flecs headers**  
   - Provide **`teng_static`** (or `BUILD_TENG_AS_STATIC`) used **only** by `engine_scene_smoke` / integration tests so scene smoke `.cpp` files can live under `tests/smoke` **without** exporting Flecs from a shared `libteng`.

Do **not** rely on “export all Flecs symbols from `libteng.so`” as the long-term fix — it widens ABI coupling and complicates Linux/macOS symbol visibility.

### D. Shipping vs development profiles

| Profile | Purpose | Suggested topology |
|---------|---------|---------------------|
| **Dev / CI** | Fast iteration, many small executables | Prefer **one** `teng` variant per configuration (shared **or** static), plus thin test exes; avoid mixing shared `teng` + separate static libs that pull Flecs headers unless invariant C holds |
| **Shipping PC game** | Single player binary + maybe anti-cheat | Often **single main binary** or exe + **explicit** DLL set (renderer/audio); engine either fully static or one “game.dll” |
| **Editor (future)** | Loads tools, possibly hot reload | More **shared** modules with clear ABI; editor-specific code never duplicates ECS |

## Migration phases

### Phase 1 — Document and enforce invariants (no big refactor)

- Add a short **`README` or CMake comment block** (repo-root or `cmake/`) stating: Flecs is **private** to `teng`; tests including Flecs must either compile inside `teng` or use **`teng_static`** (once introduced).
- Optionally add **`INTERFACE`** compile definitions or a **`TENG_BUILD_SHARED`** macro so headers can `static_assert` or comment dangerous combinations.

**Validation:** existing `agent_verify` unchanged.

### Phase 2 — Introduce optional `teng` static variant for tests

- Add **`add_library(teng STATIC ...)`** with **same sources** as shared `teng`, gated by `BUILD_SHARED_LIBS` or explicit `TENG_LIBRARY_TYPE` option (default stays shared for normal apps).
- Wire **`engine_scene_smoke`** (and optionally **`vktest`** later) to link **`teng` static** + move **`SceneSmokeTest`** sources to `tests/smoke` if desired.

**Retirement criteria:** scene smokes can live entirely under `tests/smoke` without CMake comments explaining DSO leakage; **or** team explicitly chooses “scene smokes stay in `libteng`” and deletes the static variant for tests only.

**Risk:** longer link times for static test exe; duplicate codegen size vs shared — acceptable for CI-sized binaries.

### Phase 3 — Logical split of `teng` into static sub-libraries (still one consumer)

- Factor **`teng_shader_compiler`**-style **STATIC** libs inside CMake: e.g. `teng_gfx`, `teng_engine`, merged into **either** shared `teng` **or** static `teng` via `OBJECT` or `STATIC` + `target_link_libraries`.
- Goal: **clear dependency DAG** and faster incremental builds, **not** multiple shipped shared libs yet.

**Validation:** no behavior change; binary sizes roughly stable.

### Phase 4 — Optional shared boundaries for editor / second executable

- Only when a **second major consumer** (editor, embedded viewer) exists: promote **`teng_render`** or **`teng_runtime`** to **SHARED** with a **narrow C ABI** or stable C++ API for tools.
- Keep Flecs **inside** the runtime shared lib **or** behind opaque scene handles exported from that lib.

**Unresolved until needed:** plugin ABI, hot reload boundaries, macOS framework layout.

## What should stay in the main executable (static)

- **Entry point**, CLI parsing, per-app layer wiring (`vktest` compatibility layer stays app-adjacent until retired per migration plan).
- **Glue** that must run before shared libs load (minimal).
- **Third-party** that must be singleton per process **and** is hard to isolate — preferably **not** duplicated; if duplicated, must be proven independent (usually avoid).

## What should be shared libraries (when justified)

- **Large** engine/runtime code reused by **multiple processes or tools** with compatible ABI expectations.
- **GPU drivers / platform bridges** already modeled as dynamic (Vulkan loader, Metal frameworks) — keep existing pattern.
- **Optional** editor modules once editor exists — deliberate ABI, not accidental Flecs export.

## Risks

| Risk | Mitigation |
|------|------------|
| Two Flecs runtimes | Single linkage rule; CI test that fails if `ecs_*` appears from two archives |
| CMake explosion | Phase 3 uses **internal** static libs consumed only by `teng` target |
| Link time regression | Unity builds / ninja pools; split only after profiling |
| Apple / Windows differences | Document `SHARED` vs `STATIC` defaults per OS in CMake presets |

## Validation

- `./scripts/agent_verify.sh` passes.
- `vktest --quit-after-frames 30` passes.
- No duplicate Flecs symbols in final link (`nm`, `ldd`, or linker map review when changing linkage).

## Open questions

1. Should **`vktest`** remain the primary integration harness indefinitely, or shrink to a minimal sample once `Engine` + layers stabilize?
2. Is a future **editor** a separate executable or the same binary with `--editor`? Answers drive shared-lib boundaries.
3. Should **asset tooling** ship as separate CLI binaries linking **only** `teng_assets` + core (lighter than full renderer)?

---

*This plan is specific to this repository; retire compatibility scaffolding (`CompatibilityVktestLayer`, global `ResourceManager`) per [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md), not per linkage phase unless CMake cleanup requires it.*
