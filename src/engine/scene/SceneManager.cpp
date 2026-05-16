#include "engine/scene/SceneManager.hpp"

#include <utility>

#include "core/EAssert.hpp"

namespace teng::engine {

SceneManager::SceneManager(const FlecsComponentContext& flecs_component_context)
    : flecs_component_context_(flecs_component_context) {}

Scene& SceneManager::create_scene(std::string_view name, SceneId id) {
  ASSERT(id.is_valid());
  ASSERT(!scenes_.contains(id));

  auto scene = std::make_unique<Scene>(flecs_component_context_, id, std::string{name});
  Scene& scene_ref = *scene;
  scenes_.emplace(id, std::move(scene));
  scene_metadata_.emplace(id, SceneMetadata{});
  if (!active_scene_id_.is_valid()) {
    active_scene_id_ = id;
  }
  return scene_ref;
}

bool SceneManager::destroy_scene(SceneId id) {
  if (active_scene_id_ == id) {
    clear_active_scene();
  }
  scene_metadata_.erase(id);
  return scenes_.erase(id) != 0;
}

Scene* SceneManager::find_scene(SceneId id) {
  const auto it = scenes_.find(id);
  return it == scenes_.end() ? nullptr : it->second.get();
}

const Scene* SceneManager::find_scene(SceneId id) const {
  const auto it = scenes_.find(id);
  return it == scenes_.end() ? nullptr : it->second.get();
}

bool SceneManager::set_scene_role(SceneId id, SceneRole role) {
  const auto it = scene_metadata_.find(id);
  if (it == scene_metadata_.end()) {
    return false;
  }
  it->second.role = role;
  return true;
}

SceneRole SceneManager::scene_role(SceneId id) const {
  const auto it = scene_metadata_.find(id);
  ASSERT(it != scene_metadata_.end());
  return it->second.role;
}

bool SceneManager::set_scene_execution_policy(SceneId id, SceneExecutionPolicy policy) {
  const auto it = scene_metadata_.find(id);
  if (it == scene_metadata_.end()) {
    return false;
  }
  it->second.execution_policy = policy;
  return true;
}

SceneExecutionPolicy SceneManager::scene_execution_policy(SceneId id) const {
  const auto it = scene_metadata_.find(id);
  ASSERT(it != scene_metadata_.end());
  return it->second.execution_policy;
}

bool SceneManager::set_active_scene(SceneId id) {
  if (!find_scene(id)) {
    return false;
  }
  active_scene_id_ = id;
  return true;
}

void SceneManager::clear_active_scene() { active_scene_id_ = {}; }

Scene* SceneManager::active_scene() {
  return active_scene_id_.is_valid() ? find_scene(active_scene_id_) : nullptr;
}

const Scene* SceneManager::active_scene() const {
  return active_scene_id_.is_valid() ? find_scene(active_scene_id_) : nullptr;
}

SceneRole SceneManager::active_scene_role() const {
  return active_scene_id_.is_valid() ? scene_role(active_scene_id_) : SceneRole::Runtime;
}

SceneExecutionPolicy SceneManager::active_scene_execution_policy() const {
  return active_scene_id_.is_valid() ? scene_execution_policy(active_scene_id_)
                                     : SceneExecutionPolicy{};
}

bool SceneManager::tick_active_scene(float delta_seconds) {
  Scene* scene = active_scene();
  return scene ? scene->tick(delta_seconds) : true;
}

}  // namespace teng::engine
