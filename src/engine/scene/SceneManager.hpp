#pragma once

#include <memory>
#include <string_view>
#include <unordered_map>

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneIds.hpp"

namespace teng::engine {

struct FlecsComponentContext;

enum class SceneRole {
  Runtime,
  EditDocument,
  PlaySession,
  Preview,
};

struct SceneExecutionPolicy {
  bool receives_active_input{true};
  bool advances_simulation{true};
};

class SceneManager {
 public:
  explicit SceneManager(const FlecsComponentContext& flecs_component_context);
  SceneManager(const SceneManager&) = delete;
  SceneManager& operator=(const SceneManager&) = delete;

  Scene& create_scene(std::string_view name = {}, SceneId id = make_scene_id());
  bool destroy_scene(SceneId id);
  [[nodiscard]] Scene* find_scene(SceneId id);
  [[nodiscard]] const Scene* find_scene(SceneId id) const;
  bool set_scene_role(SceneId id, SceneRole role);
  [[nodiscard]] SceneRole scene_role(SceneId id) const;
  bool set_scene_execution_policy(SceneId id, SceneExecutionPolicy policy);
  [[nodiscard]] SceneExecutionPolicy scene_execution_policy(SceneId id) const;

  bool set_active_scene(SceneId id);
  void clear_active_scene();
  [[nodiscard]] Scene* active_scene();
  [[nodiscard]] const Scene* active_scene() const;
  [[nodiscard]] SceneId active_scene_id() const { return active_scene_id_; }
  [[nodiscard]] SceneRole active_scene_role() const;
  [[nodiscard]] SceneExecutionPolicy active_scene_execution_policy() const;

  bool tick_active_scene(float delta_seconds);

 private:
  struct SceneMetadata {
    SceneRole role{SceneRole::Runtime};
    SceneExecutionPolicy execution_policy{};
  };

  const FlecsComponentContext& flecs_component_context_;
  std::unordered_map<SceneId, std::unique_ptr<Scene>> scenes_;
  std::unordered_map<SceneId, SceneMetadata> scene_metadata_;
  SceneId active_scene_id_;
};

}  // namespace teng::engine
