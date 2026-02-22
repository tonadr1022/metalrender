#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <vector>

#include "core/Config.hpp"
#include "gfx/RendererTypes.hpp"

namespace TENG_NAMESPACE {

struct Hierarchy {
  int32_t parent{k_invalid_node_id};
  int32_t first_child{k_invalid_node_id};
  int32_t next_sibling{k_invalid_node_id};
  int32_t last_sibling{k_invalid_node_id};
  int32_t level{0};
  constexpr static int32_t k_invalid_node_id = -1;
};

struct TRS {
  glm::vec3 translation{};
  glm::quat rotation{glm::identity<glm::quat>()};
  float scale{1.f};
};

static_assert(sizeof(TRS) == sizeof(float) * 8);

struct ModelInstance {
  constexpr static uint32_t invalid_id = UINT32_MAX;
  std::vector<Hierarchy> nodes;
  std::vector<TRS> local_transforms;
  std::vector<TRS> global_transforms;
  std::vector<uint32_t> mesh_ids;
  std::vector<std::vector<int32_t>> changed_this_frame;
  uint32_t tot_mesh_nodes{};
  glm::mat4 root_transform{1};
  ModelInstanceGPUHandle instance_gpu_handle;
  ModelGPUHandle model_gpu_handle;
  constexpr static size_t k_max_hierarchy_depth{24};
  void set_transform(int32_t node, const glm::mat4& transform);
  void mark_changed(int32_t node);
  // returns true if any transforms were updated
  bool update_transforms();
};

}  // namespace TENG_NAMESPACE
