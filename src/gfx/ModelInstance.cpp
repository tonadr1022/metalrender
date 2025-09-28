#pragma once

#include "ModelInstance.hpp"

void ModelInstance::set_transform(int32_t node, const glm::mat4& transform) {
  local_transforms[node] = transform;
  mark_changed(node);
}
void ModelInstance::mark_changed(int32_t node) {
  assert(node >= 0 && node < static_cast<int32_t>(nodes.size()));
  const auto level = nodes[node].level;
  changed_this_frame[level].push_back(node);
  for (int32_t child_node = nodes[node].first_child; child_node != Hierarchy::k_invalid_node_id;
       child_node = nodes[child_node].next_sibling) {
    mark_changed(child_node);
  }
}

bool ModelInstance::update_transforms() {
  bool dirty = false;

  // process level 0 separately to avoid if-check for parent existence
  for (const auto node : changed_this_frame[0]) {
    dirty = true;
    global_transforms[node] = local_transforms[node];
  }
  changed_this_frame[0].clear();

  for (size_t level = 1; level < ModelInstance::k_max_hierarchy_depth; level++) {
    auto& level_changed_nodes = changed_this_frame[level];
    dirty |= !level_changed_nodes.empty();
    for (const auto node : level_changed_nodes) {
      global_transforms[node] = global_transforms[nodes[node].parent] * local_transforms[node];
    }
    level_changed_nodes.clear();
  }

  return dirty;
}
