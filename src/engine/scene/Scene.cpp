#include "engine/scene/Scene.hpp"

#include <flecs/addons/cpp/entity.hpp>
#include <flecs/addons/cpp/mixins/pipeline/decl.hpp>
#include <string>
#include <string_view>
#include <utility>

#include "core/EAssert.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneIds.hpp"

namespace teng::engine {

Scene::Scene(SceneId id, std::string name) : id_(id), name_(std::move(name)) {
  ASSERT(id_.is_valid());
  register_components();
  register_systems();
}

Scene::~Scene() = default;

void Scene::register_components() {
  world_.component<EntityGuidComponent>("EntityGuidComponent");
  world_.component<Name>("Name");
  world_.component<Transform>("Transform");
  world_.component<LocalToWorld>("LocalToWorld");
  world_.component<Camera>("Camera");
  world_.component<DirectionalLight>("DirectionalLight");
  world_.component<MeshRenderable>("MeshRenderable");
  world_.component<SpriteRenderable>("SpriteRenderable");
}

void Scene::register_systems() {
  world_.system<const Transform, LocalToWorld>("UpdateLocalToWorld")
      .kind(flecs::OnUpdate)
      .each([](const Transform& transform, LocalToWorld& local_to_world) {
        local_to_world.value = transform_to_matrix(transform);
      });
}

flecs::entity Scene::create_entity(EntityGuid guid, std::string_view name) {
  ASSERT(guid.is_valid());
  ASSERT(!entities_by_guid_.contains(guid));

  const std::string entity_name{name};
  flecs::entity entity = entity_name.empty() ? world_.entity() : world_.entity(entity_name.c_str());
  entity.set<EntityGuidComponent>({.guid = guid});
  entity.set<Transform>({});
  entity.set<LocalToWorld>({});
  if (!entity_name.empty()) {
    entity.set<Name>({.value = entity_name});
  }

  entities_by_guid_.emplace(guid, entity);
  return entity;
}

void Scene::ensure_entity(EntityGuid guid, std::string_view name) {
  if (find_entity(guid).is_valid()) {
    return;
  }
  create_entity(guid, name);
}

void Scene::destroy_entity(EntityGuid guid) {
  const auto it = entities_by_guid_.find(guid);
  if (it == entities_by_guid_.end()) {
    return;
  }
  it->second.destruct();
  entities_by_guid_.erase(it);
}

flecs::entity Scene::find_entity(EntityGuid guid) const {
  const auto it = entities_by_guid_.find(guid);
  return it == entities_by_guid_.end() ? flecs::entity{} : it->second;
}

// Compatibility authoring helpers mutate the Flecs world through an entity handle.
bool Scene::set_transform(EntityGuid guid, const Transform& transform) const {
  const flecs::entity entity = find_entity(guid);
  if (!entity.is_valid()) {
    return false;
  }
  entity.set<Transform>(transform);
  return true;
}

bool Scene::set_local_to_world(EntityGuid guid, const LocalToWorld& local_to_world) const {
  const flecs::entity entity = find_entity(guid);
  if (!entity.is_valid()) {
    return false;
  }
  entity.set<LocalToWorld>(local_to_world);
  return true;
}

bool Scene::set_camera(EntityGuid guid, const Camera& camera) const {
  const flecs::entity entity = find_entity(guid);
  if (!entity.is_valid()) {
    return false;
  }
  entity.set<Camera>(camera);
  return true;
}

bool Scene::set_directional_light(EntityGuid guid, const DirectionalLight& light) const {
  const flecs::entity entity = find_entity(guid);
  if (!entity.is_valid()) {
    return false;
  }
  entity.set<DirectionalLight>(light);
  return true;
}

bool Scene::set_mesh_renderable(EntityGuid guid, const MeshRenderable& mesh) const {
  const flecs::entity entity = find_entity(guid);
  if (!entity.is_valid()) {
    return false;
  }
  entity.set<MeshRenderable>(mesh);
  return true;
}

bool Scene::tick(float delta_seconds) { return world_.progress(delta_seconds); }

}  // namespace teng::engine
