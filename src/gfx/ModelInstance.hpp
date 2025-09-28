#pragma once

#include <glm/mat4x4.hpp>
#include <vector>

struct Hierarchy {
  int32_t parent{k_invalid_node_id};
  int32_t first_child{k_invalid_node_id};
  int32_t next_sibling{k_invalid_node_id};
  int32_t last_sibling{k_invalid_node_id};
  int32_t level{0};
  constexpr static int32_t k_invalid_node_id = -1;
};

struct ModelInstance {
  constexpr static uint32_t invalid_id = UINT32_MAX;
  std::vector<Hierarchy> nodes;
  std::vector<glm::mat4> local_transforms;
  std::vector<glm::mat4> global_transforms;
  std::vector<uint32_t> mesh_ids;
  std::vector<std::vector<int32_t>> changed_this_frame;
  uint32_t tot_mesh_nodes{};
  glm::mat4 root_transform{1};
  constexpr static size_t k_max_hierarchy_depth{24};
  void set_transform(int32_t node, const glm::mat4& transform);
  void mark_changed(int32_t node);
  // returns true if any transforms were updated
  bool update_transforms();
};
