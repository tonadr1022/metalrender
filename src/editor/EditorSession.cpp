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

void EditorSession::sync_status_from_document() {
  last_status_ = document_controller_.last_status();
}

}  // namespace teng::editor
