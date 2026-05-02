# Flecs scene foundation (implemented)

**Status:** Initial Flecs scene foundation is in the tree. Flecs lives under **`third_party/flecs`**, linked privately into **`teng`** with headers exposed for engine scene code (single Flecs runtime per process — see [`library_linkage_architecture_plan.md`](library_linkage_architecture_plan.md)).

## What shipped

- **`engine::Scene`**: one **`flecs::world`** per scene, **`SceneId`**, core component registration, **`EntityGuid`**-based create/destroy/lookup, basic **`Transform` → `LocalToWorld`** system.
- **`engine::SceneManager`**: owned by **`Engine`**, active scene, ticked from **`Engine::tick()`**, exposed via **`EngineContext::scenes()`**.
- **Stable IDs:** **`SceneId`**, **`EntityGuid`**, **`AssetId`** (see **`SceneIds.*`**; asset identity policy continues in [`asset_registry_implementation_plan.md`](asset_registry_implementation_plan.md)).
- **Core components** (authoring): **`EntityGuidComponent`**, **`Name`**, **`Transform`**, **`LocalToWorld`**, **`Camera`**, **`DirectionalLight`**, **`MeshRenderable`**, **`SpriteRenderable`** — renderables store **`AssetId`**, not GPU handles.
- **Data scenes:** Canonical **`*.tscene.json`** (registry + **`nlohmann/json`**) — [`plans/scene_serialization_design.md`](scene_serialization_design.md). Demo content from **`scripts/generate_demo_scene_assets.py`**.
- **Tests / CI:** **`engine_scene_smoke`** and **`./scripts/agent_verify.sh`**.

## Deferred / future

- Parent/transform hierarchy (relationships or explicit parent component).
- **Composable serialization registration** — today codecs are centralized in `SceneSerialization.cpp`; game/editor-defined components should register without editing that TU (`scene_serialization_design.md` **Direction**).
- Editor **edit vs play** worlds: [`editor_play_mode_semantics.md`](editor_play_mode_semantics.md). Component registration metadata for tooling (inspector, serialization alignment).

## Principles (unchanged)

- **`Scene`** is not a polymorphic gameplay base class; behavior = ECS systems + layers + future scripting — not virtual **`update`/`render`** on scene subclasses.
- Renderer and **`RenderGraph`** stay behind **`RenderService`** / **`IRenderer`**; see [`render_service_extraction_design.md`](render_service_extraction_design.md).
