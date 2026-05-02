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
  if (!active_scene_id_.is_valid()) {
    active_scene_id_ = id;
  }
  return scene_ref;
}

bool SceneManager::destroy_scene(SceneId id) {
  if (active_scene_id_ == id) {
    clear_active_scene();
  }
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

bool SceneManager::tick_active_scene(float delta_seconds) {
  Scene* scene = active_scene();
  return scene ? scene->tick(delta_seconds) : true;
}

}  // namespace teng::engine
