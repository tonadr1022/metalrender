#include "engine/scene/SceneSmokeTest.hpp"

#include <GLFW/glfw3.h>
#include <cmath>

#include "engine/render/RenderScene.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneIds.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::engine {

namespace {

bool nearly_equal(float a, float b) { return std::abs(a - b) < 0.0001f; }

bool matrix_nearly_equal(const auto& a, const auto& b) {
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
  const auto entity = scene.create_entity(guid, "entity");

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

bool run_render_scene_extraction_smoke_test() {
  Scene scene{SceneId{100}, "render extraction"};

  const EntityGuid camera_guid{30};
  const auto camera = scene.create_entity(camera_guid, "camera");
  Transform camera_transform;
  camera_transform.translation = {1.f, 2.f, 3.f};
  camera.set<Transform>(camera_transform);
  camera.set<Camera>({.fov_y = 0.9f, .z_near = 0.25f, .z_far = 500.f, .primary = true});

  const EntityGuid light_guid{20};
  const auto light = scene.create_entity(light_guid, "light");
  light.set<DirectionalLight>({
      .direction = {0.f, -1.f, 0.f},
      .color = {0.75f, 0.8f, 1.f},
      .intensity = 3.f,
  });

  const EntityGuid mesh_guid_b{80};
  const auto mesh_b = scene.create_entity(mesh_guid_b, "mesh b");
  Transform mesh_b_transform;
  mesh_b_transform.translation = {8.f, 0.f, 0.f};
  mesh_b.set<Transform>(mesh_b_transform);
  const AssetId mesh_asset_b = AssetId::from_path("models/b.gltf");
  mesh_b.set<MeshRenderable>({.model = mesh_asset_b});

  const EntityGuid mesh_guid_a{10};
  const auto mesh_a = scene.create_entity(mesh_guid_a, "mesh a");
  Transform mesh_a_transform;
  mesh_a_transform.translation = {4.f, 0.f, 0.f};
  mesh_a.set<Transform>(mesh_a_transform);
  const AssetId mesh_asset_a = AssetId::from_path("models/a.gltf");
  mesh_a.set<MeshRenderable>({.model = mesh_asset_a});

  const EntityGuid invalid_mesh_guid{90};
  scene.create_entity(invalid_mesh_guid, "invalid mesh").set<MeshRenderable>({});

  scene.world()
      .entity("mesh missing guid")
      .set<LocalToWorld>({})
      .set<MeshRenderable>({.model = AssetId::from_path("models/missing-guid.gltf")});

  const EntityGuid sprite_guid_b{70};
  const auto sprite_b = scene.create_entity(sprite_guid_b, "sprite b");
  const AssetId sprite_asset_b = AssetId::from_path("textures/b.ktx2");
  sprite_b.set<SpriteRenderable>({
      .texture = sprite_asset_b,
      .tint = {0.2f, 0.4f, 0.6f, 0.8f},
      .sorting_layer = 1,
      .sorting_order = 0,
  });

  const EntityGuid sprite_guid_a{60};
  const auto sprite_a = scene.create_entity(sprite_guid_a, "sprite a");
  const AssetId sprite_asset_a = AssetId::from_path("textures/a.ktx2");
  sprite_a.set<SpriteRenderable>({
      .texture = sprite_asset_a,
      .tint = {1.f, 0.f, 0.f, 1.f},
      .sorting_layer = 0,
      .sorting_order = 5,
  });

  const EntityGuid invalid_sprite_guid{95};
  scene.create_entity(invalid_sprite_guid, "invalid sprite").set<SpriteRenderable>({});

  if (!scene.tick(1.f / 30.f)) {
    return false;
  }

  RenderSceneExtractStats stats;
  RenderScene render_scene = extract_render_scene(scene, RenderSceneExtractOptions{
                                                             .frame =
                                                                 {
                                                                     .frame_index = 42,
                                                                     .delta_seconds = 1.f / 30.f,
                                                                     .output_extent = {1280, 720},
                                                                 },
                                                             .stats = &stats,
                                                         });

  if (render_scene.frame.frame_index != 42 || render_scene.frame.output_extent.x != 1280 ||
      render_scene.frame.output_extent.y != 720) {
    return false;
  }
  if (stats.skipped_meshes_missing_asset != 1 || stats.skipped_sprites_missing_asset != 1) {
    return false;
  }
  if (render_scene.cameras.size() != 1 || render_scene.directional_lights.size() != 1 ||
      render_scene.meshes.size() != 2 || render_scene.sprites.size() != 2) {
    return false;
  }

  const RenderCamera& extracted_camera = render_scene.cameras.front();
  if (extracted_camera.entity != camera_guid || !extracted_camera.primary ||
      !nearly_equal(extracted_camera.fov_y, 0.9f) ||
      !matrix_nearly_equal(extracted_camera.local_to_world, transform_to_matrix(camera_transform))) {
    return false;
  }

  const RenderDirectionalLight& extracted_light = render_scene.directional_lights.front();
  if (extracted_light.entity != light_guid || !nearly_equal(extracted_light.intensity, 3.f) ||
      !nearly_equal(extracted_light.color.z, 1.f) || !extracted_light.casts_shadows) {
    return false;
  }

  if (render_scene.meshes[0].entity != mesh_guid_a ||
      render_scene.meshes[0].model != mesh_asset_a ||
      !matrix_nearly_equal(render_scene.meshes[0].local_to_world,
                           transform_to_matrix(mesh_a_transform))) {
    return false;
  }
  if (render_scene.meshes[1].entity != mesh_guid_b ||
      render_scene.meshes[1].model != mesh_asset_b) {
    return false;
  }

  if (render_scene.sprites[0].entity != sprite_guid_a ||
      render_scene.sprites[0].texture != sprite_asset_a ||
      render_scene.sprites[0].sorting_layer != 0 ||
      render_scene.sprites[0].sorting_order != 5 ||
      !nearly_equal(render_scene.sprites[0].tint.x, 1.f)) {
    return false;
  }
  if (render_scene.sprites[1].entity != sprite_guid_b ||
      render_scene.sprites[1].texture != sprite_asset_b ||
      render_scene.sprites[1].sorting_layer != 1 ||
      !nearly_equal(render_scene.sprites[1].tint.w, 0.8f)) {
    return false;
  }

  return true;
}

bool run_fps_camera_system_smoke_test() {
  Scene scene{SceneId{101}, "fps camera"};
  const EntityGuid camera_guid{40};
  const auto camera = scene.create_entity(camera_guid, "fps camera");
  camera.set<Camera>({.primary = true});
  camera.set<FpsCameraController>({
      .pitch = 0.f,
      .yaw = 0.f,
      .max_velocity = 5.f,
      .move_speed = 10.f,
      .mouse_sensitivity = 0.1f,
      .mouse_captured = true,
  });

  EngineInputSnapshot input;
  input.held_keys.insert(GLFW_KEY_W);
  input.cursor_delta = {10.f, 5.f};
  input.delta_seconds = 1.f;
  scene.set_input_snapshot(input);
  if (!scene.tick(input.delta_seconds)) {
    return false;
  }

  const auto* transform = camera.try_get<Transform>();
  const auto* controller = camera.try_get<FpsCameraController>();
  const auto* local_to_world = camera.try_get<LocalToWorld>();
  if (!transform || !controller || !local_to_world) {
    return false;
  }
  if (!nearly_equal(controller->yaw, 1.f) || !nearly_equal(controller->pitch, 0.5f)) {
    return false;
  }
  if (transform->translation.x <= 49.f || transform->translation.x >= 51.f) {
    return false;
  }
  if (!matrix_nearly_equal(local_to_world->value, transform_to_matrix(*transform))) {
    return false;
  }

  input = {};
  input.pressed_keys.insert(GLFW_KEY_ESCAPE);
  scene.set_input_snapshot(input);
  if (!scene.tick(0.f)) {
    return false;
  }
  controller = camera.try_get<FpsCameraController>();
  return controller && !controller->mouse_captured;
}

}  // namespace teng::engine
