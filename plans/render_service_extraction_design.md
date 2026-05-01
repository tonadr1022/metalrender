# Render service and extraction

**Status:** Shipped: `RenderScene` extraction, `RenderService`, default `gfx::MeshletRenderer`. Forward roadmap: [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md) (pillars, Phase 10+, compatibility).

## Shipped shape

- **`RenderScene`** (`src/engine/render/RenderScene.hpp`) — per-frame snapshot (cameras, lights, meshes, sprites); not ECS, not GPU.
- **Extractor** — Flecs → `RenderScene` (`Camera`, `DirectionalLight`, `MeshRenderable`, `SpriteRenderable`, `EntityGuidComponent`, `LocalToWorld`, …); deterministic order; skip invalid GUID/asset.
- **`IRenderer` / `RenderFrameContext`** — render boundary; **`DebugClearRenderer`** minimal impl.
- **`RenderService`** — `ShaderManager`, `RenderGraph`, uploads, `ImGuiRenderer`, `ModelGPUMgr`, `AssetId`-keyed caches, per-entity instances, `enqueue_active_scene()` → extract → present.
- **Default renderer:** `MeshletRenderer` from `RenderScene` + context.
- **Rule:** Gameplay/loaders don’t get `RenderGraph` or GPU residency as authored scene state.

## Evolution

2D/voxel milestones may **extend or version** `RenderScene` and add `IRenderer`s—allowed breakage; keep ECS out of `src/gfx` and `RenderService` as the frame owner.

## Invariants

- No `SceneManager`/Flecs in `src/gfx`.
- `RenderScene` immutable for the duration of one `IRenderer::render()` for that frame.

## Validation

`./scripts/agent_verify.sh`, `engine_scene_smoke` extraction coverage, bounded `metalrender`—[`AGENTS.md`](../AGENTS.md).
