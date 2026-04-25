#include "engine/scene/SceneSmokeTest.hpp"

#include <cmath>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::engine {

namespace {

bool nearly_equal(float a, float b) { return std::abs(a - b) < 0.0001f; }

bool matrix_nearly_equal(const glm::mat4& a, const glm::mat4& b) {
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      if (!nearly_equal(a[col][row], b[col][row])) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

bool run_scene_foundation_smoke_test() {
  SceneManager scenes;
  Scene& scene = scenes.create_scene("smoke");
  const EntityGuid guid = make_entity_guid();
  flecs::entity entity = scene.create_entity(guid, "entity");

  Transform transform;
  transform.translation = glm::vec3{3.f, 4.f, 5.f};
  transform.scale = glm::vec3{2.f, 3.f, 4.f};
  entity.set<Transform>(transform);

  if (scenes.active_scene() != &scene) {
    return false;
  }
  if (!scene.find_entity(guid).is_valid()) {
    return false;
  }
  if (!scene.tick(1.f / 60.f)) {
    return false;
  }

  const auto* local_to_world = entity.try_get<LocalToWorld>();
  if (!local_to_world) {
    return false;
  }
  if (!matrix_nearly_equal(local_to_world->value, transform_to_matrix(transform))) {
    return false;
  }

  scene.destroy_entity(guid);
  if (scene.find_entity(guid).is_valid()) {
    return false;
  }

  const AssetId asset_id = AssetId::from_path("models/sponza/../sponza/Sponza.gltf");
  const AssetId normalized_asset_id = AssetId::from_path("models/sponza/Sponza.gltf");
  return asset_id == normalized_asset_id;
}

}  // namespace teng::engine
