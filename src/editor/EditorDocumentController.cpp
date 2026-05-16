#include "editor/EditorDocumentController.hpp"

#include <format>
#include <utility>

#include "core/EAssert.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneManager.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace teng::editor {

namespace {

[[nodiscard]] std::string scene_id_text(teng::engine::SceneId id) {
  return std::to_string(id.value);
}

}  // namespace

const char* to_string(EditorMode mode) {
  switch (mode) {
    case EditorMode::Edit:
      return "Edit";
    case EditorMode::Play:
      return "Play";
  }
  return "Unknown";
}

EditorDocumentController::~EditorDocumentController() = default;

teng::Result<void> EditorDocumentController::bind(
    teng::engine::SceneManager& scenes,
    const teng::engine::SceneSerializationContext& serialization,
    teng::engine::SceneId edit_scene_id, std::optional<std::filesystem::path> path) {
  teng::engine::Scene* scene = scenes.find_scene(edit_scene_id);
  if (!scene) {
    last_status_ = "Edit scene " + scene_id_text(edit_scene_id) + " was not found";
    document_.reset();
    edit_scene_id_ = {};
    return teng::make_unexpected(last_status_);
  }

  teng::engine::scene::authoring::SceneDocumentOptions options;
  options.path = std::move(path);
  document_ = std::make_unique<teng::engine::scene::authoring::SceneDocument>(*scene, serialization,
                                                                              std::move(options));
  edit_scene_id_ = edit_scene_id;
  last_status_ = "Document opened";
  return {};
}

teng::engine::scene::authoring::SceneDocument& EditorDocumentController::document() {
  ASSERT(document_);
  return *document_;
}

const teng::engine::scene::authoring::SceneDocument& EditorDocumentController::document() const {
  ASSERT(document_);
  return *document_;
}

const std::optional<std::filesystem::path>& EditorDocumentController::path() const {
  return document().path();
}

bool EditorDocumentController::dirty() const { return document().dirty(); }

uint64_t EditorDocumentController::revision() const { return document().revision(); }

uint64_t EditorDocumentController::saved_revision() const { return document().saved_revision(); }

bool EditorDocumentController::can_save(EditorMode mode) const {
  return mode == EditorMode::Edit && bound() && document().path().has_value();
}

bool EditorDocumentController::can_create_entity(EditorMode mode) const {
  return mode == EditorMode::Edit && bound();
}

bool EditorDocumentController::can_delete_entity(
    EditorMode mode, const std::optional<teng::engine::EntityGuid>& entity) const {
  return mode == EditorMode::Edit && bound() && entity.has_value() && entity->is_valid();
}

teng::Result<void> EditorDocumentController::save(EditorMode mode) {
  if (!can_save(mode)) {
    last_status_ = "Save is not available";
    return teng::make_unexpected(last_status_);
  }

  teng::Result<void> saved = document().save();
  if (!saved) {
    last_status_ = "Save failed: " + saved.error();
    return teng::make_unexpected(last_status_);
  }
  last_status_ = "Document saved";
  return {};
}

teng::Result<teng::engine::EntityGuid> EditorDocumentController::create_entity(
    EditorMode mode, std::string_view label) {
  EditorOperationRecord operation{
      .label = "Create entity",
      .coalescing_key = "entity_create",
  };

  if (!can_create_entity(mode)) {
    operation.status = "Create entity is not available";
    record_operation(std::move(operation));
    return teng::make_unexpected(last_status_);
  }

  teng::Result<teng::engine::EntityGuid> created = document().create_entity(label);
  if (!created) {
    operation.status = "Create entity failed: " + created.error();
    record_operation(std::move(operation));
    return teng::make_unexpected(last_status_);
  }

  operation.affected_entities.push_back(*created);
  operation.succeeded = true;
  operation.status = std::format("Created entity {:016x}", created->value);
  record_operation(std::move(operation));
  return *created;
}

teng::Result<void> EditorDocumentController::delete_entity(EditorMode mode,
                                                           teng::engine::EntityGuid entity) {
  EditorOperationRecord operation{
      .label = "Delete entity",
      .affected_entities = {entity},
      .coalescing_key = "entity_delete",
  };

  if (!can_delete_entity(mode, entity)) {
    operation.status = "Delete entity is not available";
    record_operation(std::move(operation));
    return teng::make_unexpected(last_status_);
  }

  teng::Result<void> deleted = document().destroy_entity(entity);
  if (!deleted) {
    operation.status = "Delete entity failed: " + deleted.error();
    record_operation(std::move(operation));
    return teng::make_unexpected(last_status_);
  }

  operation.succeeded = true;
  operation.status = std::format("Deleted entity {:016x}", entity.value);
  record_operation(std::move(operation));
  return {};
}

void EditorDocumentController::record_operation(EditorOperationRecord operation) {
  last_status_ = operation.status;
  last_operation_ = std::move(operation);
}

}  // namespace teng::editor
