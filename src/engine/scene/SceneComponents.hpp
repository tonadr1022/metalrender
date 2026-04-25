#pragma once

#include <string>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include "engine/scene/SceneIds.hpp"

namespace teng::engine {

struct EntityGuidComponent {
  EntityGuid guid;
};

struct Name {
  std::string value;
};

struct Transform {
  glm::vec3 translation{0.f};
  glm::quat rotation{1.f, 0.f, 0.f, 0.f};
  glm::vec3 scale{1.f};
};

struct LocalToWorld {
  glm::mat4 value{1.f};
};

struct Camera {
  float fov_y{1.04719755f};
  float z_near{0.1f};
  float z_far{10000.f};
  bool primary{false};
};

struct DirectionalLight {
  glm::vec3 direction{0.35f, 1.f, 0.4f};
  glm::vec3 color{1.f};
  float intensity{1.f};
};

struct MeshRenderable {
  AssetId model;
};

struct SpriteRenderable {
  AssetId texture;
  glm::vec4 tint{1.f};
  int sorting_layer{};
  int sorting_order{};
};

[[nodiscard]] glm::mat4 transform_to_matrix(const Transform& transform);

}  // namespace teng::engine
