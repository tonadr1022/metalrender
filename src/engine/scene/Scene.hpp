#pragma once

#include <flecs.h>

#include <string>
#include <string_view>
#include <unordered_map>

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
  void ensure_entity(EntityGuid guid, std::string_view name = {});
  void destroy_entity(EntityGuid guid);
  [[nodiscard]] bool has_entity(EntityGuid guid) const;
  [[nodiscard]] flecs::entity find_entity(EntityGuid guid) const;
  [[nodiscard]] const LocalToWorld* get_local_to_world(EntityGuid guid) const;
  [[nodiscard]] const FpsCameraController* get_fps_camera_controller(EntityGuid guid) const;
  bool set_transform(EntityGuid guid, const Transform& transform) const;
  bool set_local_to_world(EntityGuid guid, const LocalToWorld& local_to_world) const;
  bool set_camera(EntityGuid guid, const Camera& camera) const;
  bool set_fps_camera_controller(EntityGuid guid, const FpsCameraController& controller) const;
  bool set_directional_light(EntityGuid guid, const DirectionalLight& light) const;
  bool set_mesh_renderable(EntityGuid guid, const MeshRenderable& mesh) const;
  void set_input_snapshot(const EngineInputSnapshot& input);

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
