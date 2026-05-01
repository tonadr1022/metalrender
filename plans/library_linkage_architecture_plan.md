# Library linkage and split architecture

**Scope:** CMake/link **topology** and **ABI** (Flecs single-runtime, shared vs static). Product-level target names and Phase 8–9 sequencing: [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md). Flecs + `libteng` context: [`tests/CMakeLists.txt`](../tests/CMakeLists.txt).

**Product:** Enforce runtime vs editor vs GPU-free tools—wrong linkage blocks clean shipped games and optional simulation modules.

**Expect breakage** when promoting splits or renaming internal targets; update consumers, not monoliths.

## Long-term requirement (shipped runtime)

**Strict match** to a typical commercial-engine **player** build (Godot export template, Unity/Unreal-style standalone game): the **shipped game executable** **statically links** **ECS (scene / Flecs)** and **engine core/runtime** (platform, assets runtime, presentation stack as required for the product) so simulation + core runtime live in the **same linkage unit** as game code—not a thin loader that depends on a separately shipped **`libteng`-style DSO** for core ECS/engine ABI. OS/driver-tier shared libraries are fine; **core ECS + runtime are not an unstable external ABI for shipping**.

**Phase 8 status:** `metalrender` links the static `teng_runtime` aggregate. The old shared
`teng` aggregate and duplicate shared object lanes have been removed; a future shared editor API
must be designed as a narrow boundary, not restored as the core runtime ABI.

## Goals

- Few intentional boundaries (not one accidental `teng` blob).
- **Exactly one Flecs runtime** per process—no duplicate `flecs_static` / DSO symbol soup.
- **Meet the long-term shipped-runtime linkage rule above**; tools/CI stay simple (static aggregates, minimal DSOs where platforms demand them).
- Keep [`AGENTS.md`](../AGENTS.md) guardrails (`agent_verify`, `metalrender` smokes, shaderc).

## Non-goals

RHI/renderer rewrites for CMake only; full consumer packaging (Steam, etc.). **Editor and internal tools** are **not** required to use the same linkage shape as shipped games (shared libs for iteration are OK); the **strict match applies to shipped/player runtime**, not every executable in the repo.

## Current layout (Phase 8 snapshot)

| Artifact | Kind | Role |
|----------|------|------|
| `teng_runtime` | **STATIC** | Full runtime aggregate (core/platform/gfx/engine objects) for shipped-style apps and runtime smokes |
| `teng_shader_compiler` | **STATIC** | Minimal shader compiler library used by `teng-shaderc` |
| `teng_engine_smoke` | **STATIC** | Smoke-test helper library → links `teng_runtime` **PUBLIC** so smoke sources can use scene APIs |
| Exes | | `metalrender` → `teng_runtime` **PRIVATE**; `engine_scene_smoke` → `teng_engine_smoke`; `teng-shaderc` → shader lib |

The removed shared `teng` target should not be reintroduced as a broad engine DSO. If a future
editor hot-reload path needs shared linkage, add a deliberately narrow `teng_editor_api`-style
boundary with explicit exports and no second Flecs runtime in-process.

## Invariants

1. **One ECS runtime** per process (Flecs, future VM, etc.).
2. **Narrow shared ABI** if you export DSOs; everything else same linkage unit or opaque handles.
3. **Flecs rule:** `teng_runtime` is the one static runtime aggregate that links `flecs::flecs_static`. Game/app code may include scene/Flecs-facing headers through the static runtime; do not “export all Flecs from libteng.so” as a shortcut for future shared tooling.
4. **Thin exes:** `main` + parse + layer push; heavy code in libraries.

**Target concept** (names flexible; matches engine plan): `teng_core` / `teng_scene` (Flecs) / `teng_render`+gfx / `teng_assets` * / `teng_editor` * — one-way DAG, no Flecs in a stray `.a` that also links **shared** `teng` unless single-instance is proven.

***Not all of these are separate shipped libs today**—internal `OBJECT`/`STATIC` buckets are fine until a second major consumer (editor) needs a promoted **SHARED** boundary with a stable API.

## Done vs next (this doc’s CMake track)

- **Done:** Invariants documented; shared `teng` retired; `teng_runtime` is the static aggregate; internal `teng_core` / `teng_platform` / `teng_gfx` / `teng_engine` object buckets feed it.
- **Next (aligns engine Phase 9+):** Optional **shared** slices only where a **second major consumer** (e.g. editor hot-reload) needs a narrow stable ABI—not for core simulation. Asset/tool CLIs link minimal `teng_*` where possible. Editor = **separate executable** per engine plan (not `--editor` on the same binary unless you later revisit and update both docs).

## Risks

| Risk | Mitigation |
|------|------------|
| Two Flecs | Single linkage model; watch link maps for duplicate `ecs_*` |
| CMake sprawl | Internal libs only; promote SHARED when a real second consumer needs it |
| Link time | Profiling, unity/pools—split for clarity first |

## Validation

`./scripts/agent_verify.sh`, `metalrender --quit-after-frames 30`, no duplicate Flecs in final link when changing topology.

## Open

- Should heavy **asset-only** tools be dedicated exes linking minimal `teng_*` set (no window/device)?
