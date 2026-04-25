#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include <flecs.h>

#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneIds.hpp"

namespace teng::engine {

class Scene {
 public:
  explicit Scene(SceneId id = make_scene_id(), std::string name = {});
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;
  Scene(Scene&&) = delete;
  Scene& operator=(Scene&&) = delete;
  ~Scene();

  [[nodiscard]] SceneId id() const { return id_; }
  [[nodiscard]] const std::string& name() const { return name_; }
  [[nodiscard]] flecs::world& world() { return world_; }
  [[nodiscard]] const flecs::world& world() const { return world_; }

  flecs::entity create_entity(EntityGuid guid = make_entity_guid(), std::string_view name = {});
  void destroy_entity(EntityGuid guid);
  [[nodiscard]] flecs::entity find_entity(EntityGuid guid) const;

  bool tick(float delta_seconds);

 private:
  void register_components();
  void register_systems();

  SceneId id_;
  std::string name_;
  flecs::world world_;
  std::unordered_map<EntityGuid, flecs::entity> entities_by_guid_;
};

}  // namespace teng::engine
