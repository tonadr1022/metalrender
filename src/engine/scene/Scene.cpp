#include "engine/scene/Scene.hpp"

#include <utility>

#include "core/EAssert.hpp"

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

bool Scene::tick(float delta_seconds) { return world_.progress(delta_seconds); }

}  // namespace teng::engine
