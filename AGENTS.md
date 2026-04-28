# Agent Instructions

## Verify (single command)

From root:

```bash
./scripts/agent_verify.sh
```

Configures CMake, builds app, runs `teng-shaderc --all`.
Add `--format` to format.

### Target names

vktest
metalrender

### Run

```bash
./build/Debug/bin/<target_name>
```

Smoke test (bounded run; avoids leaving the app open in automation):

```bash
./build/Debug/bin/vktest --quit-after-frames 30
```

Run without a frame limit (interactive, until the window is closed):

```bash
./build/Debug/bin/vktest
```

### Shader compiler (single files or ad hoc)

`./build/Debug/bin/teng-shaderc (--all | <path/to/file.comp.hlsl> [...])`  
`--all` compiles every entry-point `*.vert|frag|comp|mesh|task.hlsl` under `resources/shaders/hlsl`.

### Validating HLSL changes

Run `teng-shaderc` on shaders you change; after editing a shared `.hlsli` / header include, use `--all` (as `agent_verify.sh` does) so dependents stay in sync.

## Engine Architecture Direction

The long-term engine plan lives in:

```bash
plans/engine_runtime_migration_plan.md
```

Read it before making engine/runtime/scene/renderer architecture changes.

Current migration direction:

- `Engine::tick()` is the primary runtime primitive; `run()` should be a convenience wrapper.
- Scenes are data-first Flecs worlds, not game-specific C++ scene subclasses.
- Game behavior should come from ECS systems and future scripting, not hardcoded C++ scene classes.
- The engine should use a layer model: runtime layer, render layer, ImGui/debug layer, editor layer, future scripting layer.
- Keep Vulkan and Metal viable through existing platform/RHI abstractions; avoid new Vulkan-only assumptions in engine-level code.
- Stable scene/entity/asset IDs should be introduced early where relevant.

### Migration Scaffolding

These are temporary compatibility structures, not architecture to copy into new systems:

- `apps/vktest/TestApp.*`
- `apps/vktest/TestRenderer.*`
- `apps/vktest/TestDebugScenes.*`
- `apps/vktest/scenes/MeshletRendererTestScene.*`
- Global `ResourceManager` singleton usage

It is okay to keep scaffolding working during migration, but new engine/runtime code should not deepen dependency on it unless the change is explicitly a compatibility bridge.

## Planning Work

For design-note tasks:

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

- Minimum code that solves the problem. Nothing speculative.
- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.
