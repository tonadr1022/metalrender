#pragma once

#include <memory>
#include <string_view>
#include <unordered_map>

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneIds.hpp"

namespace teng::engine {

struct SceneComponentContext;

class SceneManager {
 public:
  explicit SceneManager(const SceneComponentContext& component_ctx);
  SceneManager(const SceneManager&) = delete;
  SceneManager& operator=(const SceneManager&) = delete;

  Scene& create_scene(std::string_view name = {}, SceneId id = make_scene_id());
  bool destroy_scene(SceneId id);
  [[nodiscard]] Scene* find_scene(SceneId id);
  [[nodiscard]] const Scene* find_scene(SceneId id) const;

  bool set_active_scene(SceneId id);
  void clear_active_scene();
  [[nodiscard]] Scene* active_scene();
  [[nodiscard]] const Scene* active_scene() const;
  [[nodiscard]] SceneId active_scene_id() const { return active_scene_id_; }

  bool tick_active_scene(float delta_seconds);

 private:
  const SceneComponentContext& component_ctx_;
  std::unordered_map<SceneId, std::unique_ptr<Scene>> scenes_;
  SceneId active_scene_id_;
};

}  // namespace teng::engine
