# Library linkage and split architecture

**Scope:** CMake/link **topology** and **ABI** (Flecs single-runtime, shared vs static). Product-level target names and Phase 8–9 sequencing: [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md). Flecs + static runtime context: [`tests/CMakeLists.txt`](../tests/CMakeLists.txt).

**Product:** Enforce runtime vs editor vs GPU-free tools—wrong linkage blocks clean shipped games and optional simulation modules.

**Expect breakage** when promoting splits or renaming internal targets; update consumers, not monoliths.

## Long-term requirement (shipped runtime)

**Strict match** to a typical commercial-engine **player** build (Godot export template, Unity/Unreal-style standalone game): the **shipped game executable** **statically links** **ECS (scene / Flecs)** and **engine core/runtime** (platform, assets runtime, presentation stack as required for the product) so simulation + core runtime live in the **same linkage unit** as game code—not a thin loader that depends on a separately shipped **`libteng`-style DSO** for core ECS/engine ABI. OS/driver-tier shared libraries are fine; **core ECS + runtime are not an unstable external ABI for shipping**.

**Phase 8 status:** `metalrender` links the static `teng_runtime` aggregate. The old shared
`teng` aggregate and duplicate shared object lanes have been removed. The runtime is now composed
from first-class static component libraries, and a future shared editor API must be designed as a
narrow boundary, not restored as the core runtime ABI.

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
| `teng_core` | **STATIC** | Core utilities and process-wide runtime helpers |
| `teng_cvars` | **STATIC** | CVar/console/debug UI helpers; kept out of GPU-free scene validation |
| `teng_assets` | **STATIC** | GPU-free asset IDs, registry, and database |
| `teng_scene` | **STATIC** | Flecs scene, components, IDs, manager, and interim TOML loader |
| `teng_scene_validate` | **STATIC** | GPU-free scaffold for future scene validate/migrate CLIs |
| `teng_platform` | **STATIC** | Window/UI platform support |
| `teng_gfx` | **STATIC** | RHI, RenderGraph, model loading, and meshlet renderer implementation |
| `teng_render` | **STATIC** | Engine render service and scene extraction |
| `teng_engine_runtime` | **STATIC** | Engine, layers, runtime frame orchestration, and `AssetService` |
| `teng_runtime` | **STATIC** | Full runtime aggregate for shipped-style apps and runtime smokes |
| `teng_shader_compiler` | **STATIC** | Minimal shader compiler library used by `teng-shaderc` |
| `teng_engine_smoke` | **STATIC** | Smoke-test helper library → links `teng_runtime` **PUBLIC** so smoke sources can use scene APIs |
| Exes | | `metalrender` → `teng_runtime` **PRIVATE**; `engine_scene_smoke` → `teng_engine_smoke`; `teng-shaderc` → shader lib |

The removed shared `teng` target should not be reintroduced as a broad engine DSO. If a future
editor hot-reload path needs shared linkage, add a deliberately narrow `teng_editor_api`-style
boundary with explicit exports and no second Flecs runtime in-process.

## Invariants

1. **One ECS runtime** per process (Flecs, future VM, etc.).
2. **Narrow shared ABI** if you export DSOs; everything else same linkage unit or opaque handles.
3. **Flecs rule:** `teng_scene` is the only component target that links `flecs::flecs_static`; runtime and validation targets reach Flecs through that single static scene library. Game/app code may include scene/Flecs-facing headers through the static runtime; do not “export all Flecs from libteng.so” as a shortcut for future shared tooling.
4. **Thin exes:** `main` + parse + layer push; heavy code in libraries.

**Current DAG:**

```text
teng_runtime -> teng_engine_runtime -> teng_render -> teng_gfx -> teng_platform -> teng_core
                                      -> teng_cvars -> teng_core
                                      -> teng_scene -> teng_assets -> teng_core

teng_scene_validate -> teng_scene + teng_assets + teng_core
```

`AssetService` remains in `teng_engine_runtime` because it currently owns `gfx::ModelLoadResult`
and `ModelInstance` in its public model-loading API. `teng_assets` is the GPU-free registry/database
slice. `teng_scene` uses engine-owned key constants rather than GLFW key constants so scene
validation does not inherit platform/window headers. `teng_runtime` is configured as a pure
aggregate target: it does not add backend compile definitions or link backend libraries directly;
those stay on the concrete platform/gfx/render component libraries that compile backend code.

***Not all of these are separate shipped libs today**—internal `OBJECT`/`STATIC` buckets are fine until a second major consumer (editor) needs a promoted **SHARED** boundary with a stable API.

## Done vs next (this doc’s CMake track)

- **Done:** Invariants documented; shared `teng` retired; `teng_runtime` is the static aggregate; internal object buckets were promoted to static component libraries; `teng_scene_validate` exists as a GPU-free static scaffold.
- **Next (aligns engine Phase 9+):** Optional **shared** slices only where a **second major consumer** (e.g. editor hot-reload) needs a narrow stable ABI—not for core simulation. Asset/tool CLIs link minimal `teng_*` where possible. Editor = **separate executable** per engine plan (not `--editor` on the same binary unless you later revisit and update both docs).

## Risks

| Risk | Mitigation |
|------|------------|
| Two Flecs | Single linkage model; watch link maps for duplicate `ecs_*` |
| CMake sprawl | Internal libs only; promote SHARED when a real second consumer needs it |
| Link time | Profiling, unity/pools—split for clarity first |

## Validation

`./scripts/agent_verify.sh`, `metalrender --quit-after-frames 30`, no duplicate Flecs in final link when changing topology. `agent_verify.sh` builds `teng_scene_validate` by default even before a CLI consumes it, so its GPU-free scaffold source stays compiled.

## Open

- Should heavy **asset-only** tools be dedicated exes linking minimal `teng_*` set (no window/device)?
