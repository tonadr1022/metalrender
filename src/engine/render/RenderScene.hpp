#pragma once

#include <cstdint>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <vector>

#include "engine/scene/SceneIds.hpp"

namespace teng::engine {

struct RenderSceneFrame {
  uint64_t frame_index{};
  float delta_seconds{};
  glm::uvec2 output_extent{};
};

struct RenderCamera {
  EntityGuid entity;
  glm::mat4 local_to_world{1.f};
  float fov_y{};
  float z_near{};
  float z_far{};
  bool primary{};
  uint32_t render_layer_mask{0xffffffffu};
};

struct RenderDirectionalLight {
  EntityGuid entity;
  glm::mat4 local_to_world{1.f};
  glm::vec3 direction{0.f, -1.f, 0.f};
  glm::vec3 color{1.f};
  float intensity{1.f};
  bool casts_shadows{true};
};

struct RenderMesh {
  EntityGuid entity;
  AssetId model;
  glm::mat4 local_to_world{1.f};
  uint32_t visibility_mask{0xffffffffu};
  bool casts_shadows{true};
};

struct RenderSprite {
  EntityGuid entity;
  AssetId texture;
  glm::mat4 local_to_world{1.f};
  glm::vec4 tint{1.f};
  int sorting_layer{};
  int sorting_order{};
};

struct RenderScene {
  RenderSceneFrame frame;
  std::vector<RenderCamera> cameras;
  std::vector<RenderDirectionalLight> directional_lights;
  std::vector<RenderMesh> meshes;
  std::vector<RenderSprite> sprites;
};

}  // namespace teng::engine
