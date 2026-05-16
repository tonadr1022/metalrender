#include "engine/scene/authoring/SceneDocument.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "engine/scene/ComponentRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneSerialization.hpp"
#include "engine/scene/SceneSerializationContext.hpp"
#include "engine/scene/authoring/SceneAuthoringCommand.hpp"
#include "engine/scene/authoring/SceneAuthoringJson.hpp"

namespace teng::engine::scene::authoring {

namespace {

[[nodiscard]] Result<const FrozenComponentRecord*> authored_component(
    const SceneSerializationContext& serialization, std::string_view component_key) {
  const FrozenComponentRecord* component =
      serialization.component_registry().find(std::string{component_key});
  if (!component) {
    return make_unexpected("component '" + std::string{component_key} + "' is not registered");
  }
  if (component->storage != ComponentStoragePolicy::Authored) {
    return make_unexpected("component '" + std::string{component_key} + "' is not authored");
  }
  return component;
}

[[nodiscard]] Result<flecs::entity> existing_entity(Scene& scene, EntityGuid entity) {
  flecs::entity flecs_entity = scene.find_entity(entity);
  if (!flecs_entity.is_valid()) {
    return make_unexpected("entity does not exist");
  }
  return flecs_entity;
}

[[nodiscard]] bool is_transform(std::string_view component_key) {
  return component_key == "teng.core.transform";
}

void refresh_derived_components_after_authoring(Scene& scene, std::string_view component_key) {
  if (is_transform(component_key)) {
    derive_local_to_world(scene);
  }
}

}  // namespace

SceneDocument::SceneDocument(Scene& scene, const SceneSerializationContext& serialization,
                             SceneDocumentOptions options)
    : scene_(scene), serialization_(serialization), path_(std::move(options.path)) {}

Result<EntityGuid> SceneDocument::create_entity(std::string_view name) {
  const EntityGuid guid = make_entity_guid();
  Result<nlohmann::ordered_json> candidate =
      candidate_with_created_entity(scene_, serialization_, guid, name);
  REQUIRED_OR_RETURN(candidate);

  scene_.create_entity(guid, name);
  derive_local_to_world(scene_);
  mark_committed(*this, SceneAuthoringMutation::EntityCreate);
  return guid;
}

Result<void> SceneDocument::rename_entity(EntityGuid entity, std::string_view name) {
  Result<nlohmann::ordered_json> candidate =
      candidate_with_renamed_entity(scene_, serialization_, entity, name);
  REQUIRED_OR_RETURN(candidate);

  Result<flecs::entity> flecs_entity = existing_entity(scene_, entity);
  REQUIRED_OR_RETURN(flecs_entity);
  (*flecs_entity).set<Name>({.value = std::string{name}});
  mark_committed(*this, SceneAuthoringMutation::EntityRename);
  return {};
}

Result<void> SceneDocument::destroy_entity(EntityGuid entity) {
  Result<nlohmann::ordered_json> candidate =
      candidate_without_entity(scene_, serialization_, entity);
  REQUIRED_OR_RETURN(candidate);

  scene_.destroy_entity(entity);
  mark_committed(*this, SceneAuthoringMutation::EntityDestroy);
  return {};
}

Result<void> SceneDocument::add_component(EntityGuid entity, std::string_view component_key) {
  Result<const FrozenComponentRecord*> component =
      authored_component(serialization_, component_key);
  REQUIRED_OR_RETURN(component);
  Result<flecs::entity> flecs_entity = existing_entity(scene_, entity);
  REQUIRED_OR_RETURN(flecs_entity);
  if ((*component)->ops.has_component_fn(*flecs_entity)) {
    return make_unexpected("entity already has component '" + std::string{component_key} + "'");
  }

  const nlohmann::ordered_json defaults = default_component_payload(**component);
  Result<nlohmann::ordered_json> candidate =
      candidate_with_component_payload(scene_, serialization_, entity, component_key, defaults);
  REQUIRED_OR_RETURN(candidate);
  Result<nlohmann::json> payload = canonical_component_payload(*candidate, entity, component_key);
  REQUIRED_OR_RETURN(payload);

  (*component)->ops.deserialize_fn(*flecs_entity, *payload);
  refresh_derived_components_after_authoring(scene_, component_key);
  mark_committed(*this, SceneAuthoringMutation::ComponentAdd);
  return {};
}

Result<void> SceneDocument::remove_component(EntityGuid entity, std::string_view component_key) {
  Result<const FrozenComponentRecord*> component =
      authored_component(serialization_, component_key);
  REQUIRED_OR_RETURN(component);
  if (!(*component)->ops.remove_fn) {
    return make_unexpected("component '" + std::string{component_key} + "' has no remove op");
  }
  Result<flecs::entity> flecs_entity = existing_entity(scene_, entity);
  REQUIRED_OR_RETURN(flecs_entity);
  if (!(*component)->ops.has_component_fn(*flecs_entity)) {
    return make_unexpected("entity does not have component '" + std::string{component_key} + "'");
  }

  Result<nlohmann::ordered_json> candidate =
      candidate_without_component(scene_, serialization_, entity, component_key);
  REQUIRED_OR_RETURN(candidate);

  (*component)->ops.remove_fn(*flecs_entity);
  refresh_derived_components_after_authoring(scene_, component_key);
  mark_committed(*this, SceneAuthoringMutation::ComponentRemove);
  return {};
}

Result<void> SceneDocument::set_component(EntityGuid entity, std::string_view component_key,
                                          const nlohmann::json& payload) {
  Result<const FrozenComponentRecord*> component =
      authored_component(serialization_, component_key);
  REQUIRED_OR_RETURN(component);
  Result<flecs::entity> flecs_entity = existing_entity(scene_, entity);
  REQUIRED_OR_RETURN(flecs_entity);

  Result<nlohmann::ordered_json> candidate =
      candidate_with_component_payload(scene_, serialization_, entity, component_key, payload);
  REQUIRED_OR_RETURN(candidate);
  Result<nlohmann::json> canonical_payload =
      canonical_component_payload(*candidate, entity, component_key);
  REQUIRED_OR_RETURN(canonical_payload);

  (*component)->ops.deserialize_fn(*flecs_entity, *canonical_payload);
  refresh_derived_components_after_authoring(scene_, component_key);
  mark_committed(*this, SceneAuthoringMutation::ComponentSet);
  return {};
}

Result<void> SceneDocument::edit_component_field(EntityGuid entity, std::string_view component_key,
                                                 std::string_view field_key,
                                                 const nlohmann::json& draft_value) {
  Result<const FrozenComponentRecord*> component =
      authored_component(serialization_, component_key);
  REQUIRED_OR_RETURN(component);
  Result<flecs::entity> flecs_entity = existing_entity(scene_, entity);
  REQUIRED_OR_RETURN(flecs_entity);

  Result<nlohmann::ordered_json> candidate = candidate_with_component_field(
      scene_, serialization_, entity, component_key, field_key, draft_value);
  REQUIRED_OR_RETURN(candidate);
  Result<nlohmann::json> payload = canonical_component_payload(*candidate, entity, component_key);
  REQUIRED_OR_RETURN(payload);

  (*component)->ops.deserialize_fn(*flecs_entity, *payload);
  refresh_derived_components_after_authoring(scene_, component_key);
  mark_committed(*this, SceneAuthoringMutation::ComponentFieldEdit);
  return {};
}

Result<void> SceneDocument::save() {
  if (!path_) {
    return make_unexpected("document path is not set");
  }
  Result<void> saved = save_scene_file(scene_, serialization_, *path_);
  REQUIRED_OR_RETURN(saved);
  mark_saved(*this);
  return {};
}

Result<void> SceneDocument::save_as(std::filesystem::path path) {
  path_ = std::move(path);
  return save();
}

void SceneDocument::mark_committed_for_authoring() { ++revision_; }

void SceneDocument::mark_saved_for_authoring() { saved_revision_ = revision_; }

}  // namespace teng::engine::scene::authoring
