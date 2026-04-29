#include "DemoSceneEcsBridge.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../common/ScenePresets.hpp"
#include "Camera.hpp"
#include "core/MathUtil.hpp"
#include "engine/assets/AssetDatabase.hpp"
#include "engine/render/RenderScene.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::gfx::demo_scene_compat {

namespace {

constexpr engine::EntityGuid k_demo_camera_guid{0x64656d6f00000001ull};
constexpr engine::EntityGuid k_demo_light_guid{0x64656d6f00000002ull};
constexpr uint64_t k_demo_mesh_guid_base{0x64656d6f10000000ull};

std::unordered_map<engine::SceneId, std::vector<engine::EntityGuid>>& authored_entities_by_scene() {
  static std::unordered_map<engine::SceneId, std::vector<engine::EntityGuid>> entities;
  return entities;
}

engine::Scene& active_or_new_scene(engine::SceneManager& scenes) {
  if (auto* scene = scenes.active_scene()) {
    return *scene;
  }
  return scenes.create_scene("vktest demo ecs scene");
}

engine::Transform transform_from_matrix(const glm::mat4& matrix) {
  engine::Transform transform;
  math::decompose_matrix(&matrix[0][0], transform.translation, transform.rotation, transform.scale);
  return transform;
}

void clear_previous_demo_entities(engine::Scene& scene) {
  auto& by_scene = authored_entities_by_scene();
  auto it = by_scene.find(scene.id());
  if (it == by_scene.end()) {
    return;
  }
  for (const engine::EntityGuid guid : it->second) {
    scene.destroy_entity(guid);
  }
  it->second.clear();
}

std::optional<engine::AssetId> resolve_demo_model_asset_id(
    const demo_scenes::DemoSceneModelInstances& model, const std::filesystem::path& resource_dir,
    const engine::assets::AssetDatabase& assets) {
  const std::filesystem::path resolved_path =
      demo_scenes::resolve_model_path(resource_dir, model.source_path);
  if (!resource_dir.empty()) {
    const std::filesystem::path relative =
        resolved_path.lexically_relative(resource_dir).lexically_normal();
    if (!relative.empty() && relative.native().front() != '.') {
      return assets.asset_id_for_source_path(relative);
    }
  }
  return assets.asset_id_for_source_path(model.source_path);
}

}  // namespace

DemoSceneEntityGuids apply_demo_preset_to_scene(engine::SceneManager& scenes,
                                                const demo_scenes::DemoScenePresetData& preset,
                                                const std::filesystem::path& resource_dir,
                                                const engine::assets::AssetDatabase& assets) {
  engine::Scene& scene = active_or_new_scene(scenes);
  clear_previous_demo_entities(scene);

  std::vector<engine::EntityGuid> authored;
  authored.reserve(2);

  scene.ensure_entity(k_demo_camera_guid, "demo camera");
  sync_demo_camera_tooling(scene, k_demo_camera_guid, preset.cam);
  authored.push_back(k_demo_camera_guid);

  scene.ensure_entity(k_demo_light_guid, "demo directional light");
  scene.set_directional_light(k_demo_light_guid, {
                                                     .direction = {0.35f, 1.f, 0.4f},
                                                     .color = glm::vec3{1.f},
                                                     .intensity = 1.f,
                                                 });
  authored.push_back(k_demo_light_guid);

  uint64_t mesh_index = 0;
  for (const auto& model : preset.models) {
    const std::optional<engine::AssetId> asset_id =
        resolve_demo_model_asset_id(model, resource_dir, assets);
    if (!asset_id) {
      continue;
    }

    for (const glm::mat4& local_to_world : model.instance_transforms) {
      const engine::EntityGuid mesh_guid{k_demo_mesh_guid_base + mesh_index};
      const std::string mesh_name = "demo mesh " + std::to_string(mesh_index);
      ++mesh_index;
      scene.ensure_entity(mesh_guid, mesh_name);
      scene.set_transform(mesh_guid, transform_from_matrix(local_to_world));
      scene.set_local_to_world(mesh_guid, {.value = local_to_world});
      scene.set_mesh_renderable(mesh_guid, {.model = *asset_id});
      authored.push_back(mesh_guid);
    }
  }

  authored_entities_by_scene()[scene.id()] = std::move(authored);
  return {.camera = k_demo_camera_guid, .light = k_demo_light_guid};
}

void sync_demo_camera_tooling(engine::Scene& scene, engine::EntityGuid camera_guid,
                              const ::Camera& camera) {
  ::Camera app_camera = camera;
  app_camera.calc_vectors();
  const glm::mat4 local_to_world = glm::inverse(app_camera.get_view_mat());
  scene.set_transform(camera_guid,
                      {.translation = app_camera.pos, .rotation = glm::quat_cast(local_to_world)});
  scene.set_local_to_world(camera_guid, {.value = local_to_world});
  scene.set_camera(camera_guid, {
                                    .fov_y = 1.04719755f,
                                    .z_near = 0.1f,
                                    .z_far = 10000.f,
                                    .primary = true,
                                });
  scene.set_fps_camera_controller(camera_guid,
                                  {
                                      .pitch = app_camera.pitch,
                                      .yaw = app_camera.yaw,
                                      .max_velocity = app_camera.max_velocity,
                                      .move_speed = app_camera.move_speed,
                                      .mouse_sensitivity = app_camera.mouse_sensitivity,
                                  });
}

void sync_demo_light_tooling(engine::Scene& scene, engine::EntityGuid light_guid,
                             engine::DirectionalLight light) {
  scene.set_directional_light(light_guid, light);
}

}  // namespace teng::gfx::demo_scene_compat
