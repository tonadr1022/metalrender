#pragma once

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <string>

#include "engine/scene/ComponentReflectionMacros.hpp"
#include "engine/scene/SceneIds.hpp"

namespace teng::engine {

struct EntityGuidComponent {
  EntityGuid guid;
};

struct Name {
  std::string value;
};

struct TENG_COMPONENT(key = "teng.core.transform", module = "teng.core", schema_version = 1,
                      storage = "Authored", visibility = "Editable", add_on_create = true)
    Transform {
  TENG_FIELD(script = "ReadWrite")
  glm::vec3 translation{0.f};

  TENG_FIELD(script = "ReadWrite")
  glm::quat rotation{1.f, 0.f, 0.f, 0.f};

  TENG_FIELD(script = "ReadWrite")
  glm::vec3 scale{1.f};
};

struct TENG_COMPONENT(key = "teng.core.local_to_world", module = "teng.core", schema_version = 1,
                      storage = "RuntimeDerived", visibility = "DebugInspectable",
                      add_on_create = true) LocalToWorld {
  TENG_FIELD(script = "None")
  glm::mat4 value{1.f};
};

struct TENG_COMPONENT(key = "teng.core.camera", module = "teng.core", schema_version = 1,
                      storage = "Authored", visibility = "Editable") Camera {
  TENG_FIELD(script = "ReadWrite")
  float fov_y{1.04719755f};

  TENG_FIELD(script = "ReadWrite")
  float z_near{0.1f};

  TENG_FIELD(script = "ReadWrite")
  float z_far{10000.f};

  TENG_FIELD(script = "ReadWrite")
  bool primary{false};
};

struct TENG_COMPONENT(key = "teng.core.fps_camera_controller", module = "teng.core",
                      schema_version = 1, storage = "RuntimeSession",
                      visibility = "DebugInspectable") FpsCameraController {
  TENG_FIELD(script = "None")
  float pitch{};

  TENG_FIELD(script = "None")
  float yaw{};

  TENG_FIELD(script = "None")
  float max_velocity{5.f};

  TENG_FIELD(script = "None")
  float move_speed{10.f};

  TENG_FIELD(script = "None")
  float mouse_sensitivity{0.1f};

  TENG_FIELD(script = "None")
  float look_pitch_sign{1.f};

  TENG_FIELD(script = "None")
  bool mouse_captured{};
};

struct TENG_COMPONENT(key = "teng.core.directional_light", module = "teng.core", schema_version = 1,
                      storage = "Authored", visibility = "Editable") DirectionalLight {
  TENG_FIELD(script = "ReadWrite")
  glm::vec3 direction{0.35f, 1.f, 0.4f};

  TENG_FIELD(script = "ReadWrite")
  glm::vec3 color{1.f};

  TENG_FIELD(script = "ReadWrite")
  float intensity{1.f};
};

struct TENG_COMPONENT(key = "teng.core.mesh_renderable", module = "teng.core", schema_version = 1,
                      storage = "Authored", visibility = "Editable") MeshRenderable {
  TENG_FIELD(asset_kind = "model", script = "ReadWrite")
  AssetId model;
};

struct TENG_COMPONENT(key = "teng.core.sprite_renderable", module = "teng.core", schema_version = 1,
                      storage = "Authored", visibility = "Editable") SpriteRenderable {
  TENG_FIELD(asset_kind = "texture", script = "ReadWrite")
  AssetId texture;

  TENG_FIELD(script = "ReadWrite")
  glm::vec4 tint{1.f};

  TENG_FIELD(script = "ReadWrite")
  int sorting_layer{};

  TENG_FIELD(script = "ReadWrite")
  int sorting_order{};
};

[[nodiscard]] glm::mat4 transform_to_matrix(const Transform& transform);

}  // namespace teng::engine
