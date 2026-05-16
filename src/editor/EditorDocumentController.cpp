#include "editor/EditorDocumentController.hpp"

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

}  // namespace teng::editor
