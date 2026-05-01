# Render Service Extraction Design

**Status:** Phase 3 render extraction, Phase 5 meshlet **`IRenderer`** integration, and the asset/residency bridge are in place. This note summarizes the architecture; roadmap items (2D path, editor, library splits) are in [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md).

## What shipped

- **`RenderScene`** (`src/engine/render/RenderScene.hpp`) is a per-frame, renderer-neutral snapshot: cameras, directional lights, meshes, sprites. It is not an ECS view and not GPU state.
- **`RenderSceneExtractor`** fills that snapshot from Flecs components (`Camera`, `DirectionalLight`, `MeshRenderable`, `SpriteRenderable`, `EntityGuidComponent`, `LocalToWorld`, etc.) with deterministic ordering and skip counters for invalid GUIDs/assets.
- **`IRenderer`** + **`RenderFrameContext`** are the renderer-facing boundary; **`DebugClearRenderer`** remains a minimal diagnostic implementation.
- **`RenderService`** (`src/engine/render/RenderService.*`) is engine-owned: **`ShaderManager`**, **`RenderGraph`**, upload/copy helpers, **`ImGuiRenderer`**, **`ModelGPUMgr`**, shared mesh GPU caches keyed by **`AssetId`**, per-entity model instances, active **`IRenderer`**, and **`enqueue_active_scene()`** which extracts from the active **`Scene`** and presents.
- Default active renderer is **`gfx::MeshletRenderer`** (`src/gfx/renderer/MeshletRenderer.*`), which builds passes from **`RenderScene`** and **`RenderFrameContext`**.
- **Gameplay, loaders, and ECS** do not receive **`RenderGraph`**, RHI handles, or GPU residency handles as authored scene state; extraction stays CPU-side until the render service invokes the renderer.

## Rules to preserve

- Keep **`Scene` / `SceneManager` / Flecs** out of **`src/gfx`**; keep **`RenderService`** as the engine-owned frame boundary.
- **`RenderScene`** stays immutable for the duration of a given frame’s **`IRenderer::render()`** call.

## Validation

`./scripts/agent_verify.sh`, `engine_scene_smoke` (extraction coverage), and bounded **`metalrender`** smoke runs per [`AGENTS.md`](../AGENTS.md).
