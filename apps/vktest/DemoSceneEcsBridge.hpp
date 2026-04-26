#pragma once

#include <filesystem>
#include <optional>

#include "../common/ScenePresets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneIds.hpp"

class Camera;

namespace teng::engine {
class Scene;
class SceneManager;
}  // namespace teng::engine

namespace teng::gfx::demo_scene_compat {

struct DemoSceneEntityGuids {
  engine::EntityGuid camera;
  engine::EntityGuid light;
};

[[nodiscard]] DemoSceneEntityGuids apply_demo_preset_to_scene(
    engine::SceneManager& scenes, const demo_scenes::DemoScenePresetData& preset,
    const std::filesystem::path& resource_dir);

void sync_demo_camera_tooling(engine::Scene& scene, engine::EntityGuid camera_guid,
                              const ::Camera& camera);
void sync_demo_light_tooling(engine::Scene& scene, engine::EntityGuid light_guid,
                             engine::DirectionalLight light);
void sync_loaded_model_transforms(engine::Scene& scene);
void clear_loaded_models(engine::SceneManager& scenes);

void register_asset_path(engine::AssetId asset_id, std::filesystem::path path);
[[nodiscard]] std::optional<std::filesystem::path> resolve_model_path(engine::AssetId asset_id);

[[nodiscard]] bool run_demo_scene_authoring_smoke_test();

}  // namespace teng::gfx::demo_scene_compat
