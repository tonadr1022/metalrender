# Asset registry and runtime asset service

**Status:** Runtime bridge is implemented: stable **`AssetId`** (128-bit + text format), **`AssetRegistry`** / **`AssetDatabase`** (sidecars, project scan, dependencies, redirects, tombstones), engine **`AssetService`** for CPU **`ModelAsset`** load/cache, and **`RenderService`**-owned model GPU residency (**`ModelGPUMgr`** upload + per-entity instances). Demo scenes and model registration are driven by **`scripts/generate_demo_scene_assets.py`** and loaded via **`SceneAssetLoader`** (interim **`*.tscene.toml`**) into **`SceneManager`** until Phase 12 replaces the loader with **JSON** **`*.tscene.json`** per [`plans/scene_serialization_design.md`](scene_serialization_design.md).

**Remaining (see also Phase 6 in [`engine_runtime_migration_plan.md`](engine_runtime_migration_plan.md)):** read-only **cooked runtime manifests** (dependency closure for shipped scenes, artifact paths) so runtime startup can avoid full source-tree scanning where desired.

## Implemented layout (reference)

| Area | Role |
|------|------|
| `src/engine/scene/SceneIds.*` | **`AssetId`** parse/format; **`AssetId::from_path()`** exists only for tests/legacy helpers — **must not** appear in serialized scene data |
| `src/engine/assets/` | Registry records, database, scan/register/move/delete/fixup, **`AssetService`** |
| `src/engine/scene/SceneAssetLoader.*` | Interim scene load (**`*.tscene.toml`**) → **`SceneManager`**; **removed** once Phase 12 registry + JSON lands |
| `src/engine/render/RenderService.*` | Resolves extraction **`AssetId`** → **`AssetService`** → GPU residency |
| `apps/metalrender/main.cpp` | CLI **`--scene`**, **`resources/project.toml`** **`startup_scene`**, **`--quit-after-frames`** |
| `scripts/generate_demo_scene_assets.py` | Idempotent demo asset + scene generation |

## Non‑negotiables (carry forward)

1. Scene files reference assets by stable **`AssetId`**, not source paths or GPU handles.
2. CPU import/load (**`AssetService`**) and GPU residency (**`RenderService`** / renderer machinery) stay separate.
3. Asset services are engine-owned; no globals for asset identity or loading.
4. Registry/graph behavior (dependencies, delete blocking, redirects) stays defined in **`src/engine/assets`** and tests — see existing smoke coverage under **`engine_scene_smoke`**.

## Validation

```bash
./scripts/agent_verify.sh
./build/Debug/bin/metalrender --quit-after-frames 30
./build/Debug/bin/metalrender --scene resources/scenes/demo_cube.tscene.json --quit-after-frames 30
```

*(Until Phase 12 migrates repo demos, smoke may still use `*.tscene.toml`; align paths when canonical JSON ships — [`scene_serialization_design.md`](scene_serialization_design.md).)*
