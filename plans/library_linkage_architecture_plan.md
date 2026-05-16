# Library linkage and split architecture

**Scope:** CMake/link **topology** and **ABI** (Flecs single-runtime, shared vs static). Product-level target names and Phase 8–9 sequencing: [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md). Flecs + static runtime context: [`tests/CMakeLists.txt`](../tests/CMakeLists.txt).

**Product:** Enforce runtime vs editor vs GPU-free tools—wrong linkage blocks clean shipped games and optional simulation modules.

**Expect breakage** when promoting splits or renaming internal targets; update consumers, not monoliths.

## Long-term requirement (shipped runtime)

**Strict match** to a typical commercial-engine **player** build (Godot export template, Unity/Unreal-style standalone game): the **shipped game executable** **statically links** **ECS (scene / Flecs)** and **engine core/runtime** (platform, assets runtime, presentation stack as required for the product) so simulation + core runtime live in the **same linkage unit** as game code—not a thin loader that depends on a separately shipped **`libteng`-style DSO** for core ECS/engine ABI. OS/driver-tier shared libraries are fine; **core ECS + runtime are not an unstable external ABI for shipping**.

**Phase 8 status:** `metalrender` links the `teng_runtime` interface aggregate over static runtime component libraries. The runtime is now composed
from first-class static component libraries, and a future shared editor API must be designed as a
narrow boundary, not restored as the core runtime ABI.

## Goals

- Few intentional boundaries (not one accidental `teng` blob).
- **Exactly one Flecs runtime** per process—no duplicate `flecs_static` / DSO symbol soup.
- **Meet the long-term shipped-runtime linkage rule above**; tools/CI stay simple (static aggregates, minimal DSOs where platforms demand them).
- Keep [`AGENTS.md`](../AGENTS.md) guardrails (`agent_verify`, `metalrender` smokes, shaderc).

## Non-goals

RHI/renderer rewrites for CMake only; full consumer packaging (Steam, etc.). **Editor and internal tools** are **not** required to use the same linkage shape as shipped games (shared libs for iteration are OK); the **strict match applies to shipped/player runtime**, not every executable in the repo.

## Layout: Read CmakeLists thoroughly

## Invariants

1. **One ECS runtime** per process (Flecs, future VM, etc.).
2. **Narrow shared ABI** if you export DSOs; everything else same linkage unit or opaque handles.
3. **Flecs rule:** `teng_scene` is the only component target that links `flecs::flecs_static`; runtime and validation targets reach Flecs through that single static scene library. Game/app code may include scene/Flecs-facing headers through the static runtime; do not “export all Flecs from libteng.so” as a shortcut for future shared tooling.
4. **Thin exes:** `main` + parse + layer push; heavy code in libraries.