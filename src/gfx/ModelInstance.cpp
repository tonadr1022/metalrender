#include "ModelInstance.hpp"

#include "core/EAssert.hpp"
#include "core/MathUtil.hpp"
#include "glm/gtc/type_ptr.hpp"

namespace {

TRS to_trs(const glm::mat4& transform) {
  glm::vec3 t;
  glm::quat r;
  glm::vec3 s;
  math::decompose_matrix(glm::value_ptr(transform), t, r, s);
  return {t, r, glm::max(s.x, glm::max(s.y, s.z))};
}

}  // namespace

void ModelInstance::set_transform(int32_t node, const glm::mat4& transform) {
  local_transforms[node] = to_trs(transform);
  mark_changed(node);
}

void ModelInstance::mark_changed(int32_t node) {
  assert(node >= 0 && node < static_cast<int32_t>(nodes.size()));
  const auto level = nodes[node].level;
  ASSERT(level < static_cast<int32_t>(changed_this_frame.size()));
  changed_this_frame[level].push_back(node);
  for (int32_t child_node = nodes[node].first_child; child_node != Hierarchy::k_invalid_node_id;
       child_node = nodes[child_node].next_sibling) {
    mark_changed(child_node);
  }
}

bool ModelInstance::update_transforms() {
  bool dirty = false;
  ASSERT(changed_this_frame.size() > 0);

  // process level 0 separately to avoid if-check for parent existence
  for (const auto node : changed_this_frame[0]) {
    dirty = true;
    global_transforms[node] = local_transforms[node];
  }
  changed_this_frame[0].clear();

  for (size_t level = 1; level < changed_this_frame.size(); level++) {
    auto& level_changed_nodes = changed_this_frame[level];
    dirty |= !level_changed_nodes.empty();
    for (const auto node : level_changed_nodes) {
      const auto parent_rot = global_transforms[nodes[node].parent].rotation;
      const auto parent_scale = global_transforms[nodes[node].parent].scale;
      const auto parent_translation = global_transforms[nodes[node].parent].translation;
      global_transforms[node].rotation = parent_rot * local_transforms[node].rotation;
      global_transforms[node].scale = parent_scale * local_transforms[node].scale;
      global_transforms[node].translation =
          parent_translation + parent_rot * (parent_scale * local_transforms[node].translation);
    }
    level_changed_nodes.clear();
  }

  return dirty;
}
