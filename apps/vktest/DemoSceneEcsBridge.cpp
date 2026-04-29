#include "DemoSceneEcsBridge.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../common/ScenePresets.hpp"
#include "Camera.hpp"
#include "ResourceManager.hpp"
#include "core/MathUtil.hpp"
#include "engine/render/RenderScene.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneManager.hpp"
#include "gfx/ModelInstance.hpp"

namespace teng::gfx::demo_scene_compat {

namespace {

constexpr engine::EntityGuid k_demo_camera_guid{0x64656d6f00000001ull};
constexpr engine::EntityGuid k_demo_light_guid{0x64656d6f00000002ull};
constexpr uint64_t k_demo_mesh_guid_base{0x64656d6f10000000ull};

std::unordered_map<engine::SceneId, std::vector<engine::EntityGuid>>& authored_entities_by_scene() {
  static std::unordered_map<engine::SceneId, std::vector<engine::EntityGuid>> entities;
  return entities;
}

std::unordered_map<engine::AssetId, std::filesystem::path>& asset_paths() {
  static std::unordered_map<engine::AssetId, std::filesystem::path> paths;
  return paths;
}

struct LoadedModel {
  engine::EntityGuid entity;
  ModelHandle handle;
};

struct PendingModelGroup {
  std::vector<engine::EntityGuid> entities;
};

std::unordered_map<engine::SceneId, std::vector<LoadedModel>>& loaded_models_by_scene() {
  static std::unordered_map<engine::SceneId, std::vector<LoadedModel>> models;
  return models;
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

void free_loaded_models(engine::SceneId scene_id) {
  auto& loaded_by_scene = loaded_models_by_scene();
  const auto it = loaded_by_scene.find(scene_id);
  if (it == loaded_by_scene.end()) {
    return;
  }
  if (ResourceManager::is_initialized()) {
    for (const LoadedModel& model : it->second) {
      ResourceManager::get().free_model(model.handle);
    }
  }
  loaded_by_scene.erase(it);
}

void clear_previous_demo_entities(engine::Scene& scene) {
  free_loaded_models(scene.id());

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

void load_demo_models(engine::SceneId scene_id, std::span<const PendingModelGroup> groups,
                      std::span<const ResourceManager::InstancedModelLoadRequest> requests) {
  if (!ResourceManager::is_initialized() || requests.empty()) {
    return;
  }

  std::vector<std::vector<ModelHandle>> loaded =
      ResourceManager::get().load_instanced_models(requests);
  ASSERT(loaded.size() == groups.size());

  std::vector<LoadedModel>& scene_models = loaded_models_by_scene()[scene_id];
  for (size_t group_idx = 0; group_idx < loaded.size(); ++group_idx) {
    const PendingModelGroup& group = groups[group_idx];
    const std::vector<ModelHandle>& handles = loaded[group_idx];
    ASSERT(handles.size() == group.entities.size());
    for (size_t handle_idx = 0; handle_idx < handles.size(); ++handle_idx) {
      scene_models.push_back(LoadedModel{
          .entity = group.entities[handle_idx],
          .handle = handles[handle_idx],
      });
    }
  }
}

}  // namespace

DemoSceneEntityGuids apply_demo_preset_to_scene(engine::SceneManager& scenes,
                                                const demo_scenes::DemoScenePresetData& preset,
                                                const std::filesystem::path& resource_dir) {
  engine::Scene& scene = active_or_new_scene(scenes);
  clear_previous_demo_entities(scene);

  std::vector<engine::EntityGuid> authored;
  authored.reserve(2);
  std::vector<PendingModelGroup> pending_groups;
  pending_groups.reserve(preset.models.size());
  std::vector<ResourceManager::InstancedModelLoadRequest> load_requests;
  load_requests.reserve(preset.models.size());

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
    const engine::AssetId asset_id = engine::AssetId::from_path(model.source_path);
    const std::filesystem::path resolved_path =
        demo_scenes::resolve_model_path(resource_dir, model.source_path);
    register_asset_path(asset_id, resolved_path);

    PendingModelGroup& pending_group = pending_groups.emplace_back();
    pending_group.entities.reserve(model.instance_transforms.size());
    ResourceManager::InstancedModelLoadRequest& load_request = load_requests.emplace_back();
    load_request.path = resolved_path;
    load_request.instance_transforms.reserve(model.instance_transforms.size());

    for (const glm::mat4& local_to_world : model.instance_transforms) {
      const engine::EntityGuid mesh_guid{k_demo_mesh_guid_base + mesh_index};
      const std::string mesh_name = "demo mesh " + std::to_string(mesh_index);
      ++mesh_index;
      scene.ensure_entity(mesh_guid, mesh_name);
      scene.set_transform(mesh_guid, transform_from_matrix(local_to_world));
      scene.set_local_to_world(mesh_guid, {.value = local_to_world});
      scene.set_mesh_renderable(mesh_guid, {.model = asset_id});
      authored.push_back(mesh_guid);
      pending_group.entities.push_back(mesh_guid);
      load_request.instance_transforms.push_back(local_to_world);
    }
  }

  authored_entities_by_scene()[scene.id()] = std::move(authored);
  load_demo_models(scene.id(), pending_groups, load_requests);
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

void sync_loaded_model_transforms(engine::Scene& scene) {
  const auto loaded_it = loaded_models_by_scene().find(scene.id());
  if (loaded_it == loaded_models_by_scene().end() || !ResourceManager::is_initialized()) {
    return;
  }

  for (const LoadedModel& loaded : loaded_it->second) {
    const engine::LocalToWorld* local_to_world = scene.get_local_to_world(loaded.entity);
    if (!local_to_world) {
      continue;
    }
    if (ModelInstance* model = ResourceManager::get().get_model(loaded.handle)) {
      model->set_transform(0, local_to_world->value);
      model->update_transforms();
    }
  }
}

void clear_loaded_models(engine::SceneManager& scenes) {
  if (engine::Scene* scene = scenes.active_scene()) {
    free_loaded_models(scene->id());
  }
}

void register_asset_path(engine::AssetId asset_id, std::filesystem::path path) {
  if (!asset_id.is_valid()) {
    return;
  }
  asset_paths()[asset_id] = std::move(path);
}

std::optional<std::filesystem::path> resolve_model_path(engine::AssetId asset_id) {
  const auto& paths = asset_paths();
  const auto it = paths.find(asset_id);
  if (it == paths.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace teng::gfx::demo_scene_compat
