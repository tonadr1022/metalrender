# Agent Instructions

## Verify (single command)

From root:

```bash
./scripts/agent_verify.sh
```

Configures CMake, builds apps and scaffold libraries, runs `engine_scene_smoke`, and runs shader compile checks.
Add `--format` to format.

### Target names

metalrender
teng-shaderc
engine_scene_smoke
teng-scene-tool

Common internal library targets include `teng_runtime` (interface aggregate over static runtime
component libraries) and components such as `teng_core`, `teng_assets`, `teng_scene`, `teng_render`,
`teng_gfx`, `teng_engine_runtime`, and `teng_scene_tool_lib`. `teng-scene-tool` is intentionally built
by default verification so the GPU-free scene CLI scaffold does not silently break.

### Run

```bash
./build/Debug/bin/<target_name>
```

Smoke test (bounded run; avoids leaving the app open in automation):

```bash
./build/Debug/bin/metalrender --quit-after-frames 30
./build/Debug/bin/metalrender --scene resources/scenes/demo_cube.tscene.toml --quit-after-frames 30
```

Run without a frame limit (interactive, until the window is closed):

```bash
./build/Debug/bin/metalrender
./build/Debug/bin/metalrender --scene resources/scenes/demo_cube.tscene.toml
```

### Shader compiler (single files or ad hoc)

`./build/Debug/bin/teng-shaderc (--all | <path/to/file.comp.hlsl> [...])`  
`--all` compiles every entry-point `*.vert|frag|comp|mesh|task.hlsl` under `resources/shaders/hlsl`.

### Validating HLSL changes

Run `teng-shaderc` on shaders you change; after editing a shared `.hlsli` / header include, use `--all` (as `agent_verify.sh` does) so dependents stay in sync.

## Engine Architecture Direction

The engine direction and phased roadmap live in:

```bash
plans/engine_runtime_migration_plan.md
```

Read it before making engine/runtime/scene/renderer architecture changes. Related notes: `plans/library_linkage_architecture_plan.md`, `plans/scene_serialization_design.md`, `plans/render_service_extraction_design.md`.

**Scope honesty:** Many “full engine” concerns (deep editor UX, scripting VM/bindings, player saves, store export, networking, input rebinding) are **not** fully planned—see [Scope honesty: not designed yet](plans/engine_runtime_migration_plan.md#scope-honesty-not-designed-yet) in the migration plan so agents and humans do not assume they exist.

Direction (high level):

- `Engine::tick()` is the primary runtime primitive; `run()` is a convenience wrapper.
- Scenes are data-first Flecs worlds, not game-specific C++ scene subclasses.
- Game behavior comes from ECS systems and future scripting, not monolithic C++ scene classes.
- Layer model: runtime, render, ImGui/debug, editor (separate target), future scripting.
- Presentation stays behind `RenderScene` / `IRenderer`; simulation modules (physics, animation, audio) extend via components + systems + narrow services—do not collapse everything into meshlet code.
- Vulkan and Metal stay viable through platform/RHI abstractions; avoid Vulkan-only assumptions in engine-level code.
- Stable scene/entity/asset IDs remain the authored identity model; **on-disk scene formats and `RenderScene` extraction may break** as 2D/editor/cooked pipelines land—see the plan’s compatibility section. Things do not need to be backward compatible. we are far from 1.0. big refactors are allowed.

### Guardrails

Do not reintroduce deleted compatibility harnesses, old C++ demo preset bridges, or monolithic app-side renderer/scene types removed during the engine migration.

## Planning Work

For design-note and spec tasks:

- Do not write implementation code unless explicitly asked.
- Inspect the relevant code before planning.
- Write plans under `plans/`.
- Mark migration scaffolding clearly and include retirement criteria.
- Include phased implementation steps, risks, validation, and unresolved questions.
- Keep plans specific to this repository.

### Cpp Coding Tips

- const correctness
- don't cast when you don't need to.
- don't use min/max, null checks when you don't need to. Many times invariants are true about these things.
- Make required invariants explicit at validation boundaries; don't propagate optional/null states after validation.

### Required Guidelines

- Big refactors are allowed and encouraged in the name of achieving long term direction
- No error handling for impossible scenarios.
