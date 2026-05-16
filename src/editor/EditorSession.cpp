#include "editor/EditorSession.hpp"

#include <utility>

#include "engine/Engine.hpp"
#include "engine/scene/SceneManager.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace teng::editor {

teng::Result<void> EditorSession::bind(teng::engine::EngineContext& ctx,
                                       teng::engine::SceneId edit_scene_id,
                                       std::optional<std::filesystem::path> path) {
  return bind(ctx.scenes(), ctx.scene_serialization(), edit_scene_id, std::move(path));
}

teng::Result<void> EditorSession::bind(teng::engine::SceneManager& scenes,
                                       const teng::engine::SceneSerializationContext& serialization,
                                       teng::engine::SceneId edit_scene_id,
                                       std::optional<std::filesystem::path> path) {
  mode_ = EditorMode::Edit;
  selection_.clear();
  const teng::Result<void> bound =
      document_controller_.bind(scenes, serialization, edit_scene_id, std::move(path));
  sync_status_from_document();
  if (!bound) {
    return teng::make_unexpected(last_status_);
  }
  return {};
}

teng::Result<void> EditorSession::save() {
  const teng::Result<void> saved = document_controller_.save(mode_);
  sync_status_from_document();
  if (!saved) {
    return teng::make_unexpected(last_status_);
  }
  return {};
}

teng::Result<teng::engine::EntityGuid> EditorSession::create_entity(std::string_view label) {
  teng::Result<teng::engine::EntityGuid> created =
      document_controller_.create_entity(mode_, label);
  sync_status_from_document();
  if (!created) {
    return teng::make_unexpected(last_status_);
  }
  selection_.select(*created);
  return *created;
}

teng::Result<void> EditorSession::delete_selected_entity() {
  const std::optional<teng::engine::EntityGuid> selected = selection_.selected();
  if (!selected) {
    const teng::Result<void> deleted = document_controller_.delete_entity(mode_, {});
    sync_status_from_document();
    return teng::make_unexpected(last_status_);
  }

  const teng::Result<void> deleted = document_controller_.delete_entity(mode_, *selected);
  sync_status_from_document();
  if (!deleted) {
    return teng::make_unexpected(last_status_);
  }
  selection_.clear();
  return {};
}

void EditorSession::sync_status_from_document() {
  last_status_ = document_controller_.last_status();
}

}  // namespace teng::editor
