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
namespace assets {
class AssetDatabase;
}
}  // namespace teng::engine

namespace teng::gfx::demo_scene_compat {

struct DemoSceneEntityGuids {
  engine::EntityGuid camera;
  engine::EntityGuid light;
};

[[nodiscard]] DemoSceneEntityGuids apply_demo_preset_to_scene(
    engine::SceneManager& scenes, const demo_scenes::DemoScenePresetData& preset,
    const std::filesystem::path& resource_dir, const engine::assets::AssetDatabase& assets);

void sync_demo_camera_tooling(engine::Scene& scene, engine::EntityGuid camera_guid,
                              const ::Camera& camera);
void sync_demo_light_tooling(engine::Scene& scene, engine::EntityGuid light_guid,
                             engine::DirectionalLight light);

}  // namespace teng::gfx::demo_scene_compat
