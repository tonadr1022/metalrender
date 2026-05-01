# Engine tick and layer stack (implemented)

**Status:** Phase 1 is done. This file is a short record of what exists; sequencing and forward work live in [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md).

## What shipped

- **`engine::Engine`** (`src/engine/Engine.*`) owns resource path setup, CVar lifecycle, platform window, RHI device, swapchain, frame timing, queued input dispatch, ImGui frame begin/end, **`Engine::tick()`** as the primary API, and **`Engine::run()`** as a convenience loop over `tick()`.
- **`EngineConfig`**, **`EngineContext`**, **`EngineTime`** wire configuration and per-frame state into layers and services.
- **`Layer`** / **`LayerStack`** provide ordered **`on_attach`**, **`on_detach`**, **`on_update`**, **`on_render`**, **`on_imgui`**, **`on_event`** hooks. Runtime hosts (e.g. `apps/metalrender/main.cpp`) push scene and overlay layers and call `Engine::run()` or drive `tick()` manually later for editor-style control.
- Backend selection stays in **`EngineConfig`** with platform defaults and explicit Vulkan/Metal choices; engine-level code stays backend-neutral where possible.

## Validation

Same as repo guardrails: `./scripts/agent_verify.sh` and bounded `metalrender --quit-after-frames 30` (see [`AGENTS.md`](../AGENTS.md)).
