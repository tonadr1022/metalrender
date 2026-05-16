#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/Result.hpp"
#include "editor/EditorDocumentController.hpp"
#include "engine/scene/SceneIds.hpp"

namespace teng::engine {
class EngineContext;
class SceneManager;
struct SceneSerializationContext;
}  // namespace teng::engine

namespace teng::editor {

class EditorSelection {
 public:
  void select(teng::engine::EntityGuid entity) { selected_ = entity; }
  void clear() { selected_.reset(); }
  [[nodiscard]] const std::optional<teng::engine::EntityGuid>& selected() const {
    return selected_;
  }

 private:
  std::optional<teng::engine::EntityGuid> selected_;
};

class EditorSession {
 public:
  [[nodiscard]] teng::Result<void> bind(teng::engine::EngineContext& ctx,
                                        teng::engine::SceneId edit_scene_id,
                                        std::optional<std::filesystem::path> path = std::nullopt);
  [[nodiscard]] teng::Result<void> bind(
      teng::engine::SceneManager& scenes,
      const teng::engine::SceneSerializationContext& serialization,
      teng::engine::SceneId edit_scene_id,
      std::optional<std::filesystem::path> path = std::nullopt);

  [[nodiscard]] bool bound() const { return document_controller_.bound(); }
  [[nodiscard]] EditorMode mode() const { return mode_; }
  [[nodiscard]] EditorSelection& selection() { return selection_; }
  [[nodiscard]] const EditorSelection& selection() const { return selection_; }
  [[nodiscard]] EditorDocumentController& document_controller() { return document_controller_; }
  [[nodiscard]] const EditorDocumentController& document_controller() const {
    return document_controller_;
  }
  [[nodiscard]] const std::string& last_status() const { return last_status_; }

  [[nodiscard]] bool can_save() const { return document_controller_.can_save(mode_); }
  [[nodiscard]] bool can_reload() const { return false; }
  [[nodiscard]] bool can_play() const { return false; }
  [[nodiscard]] bool can_stop() const { return false; }
  [[nodiscard]] bool can_create_entity() const { return false; }
  [[nodiscard]] bool can_delete_selected_entity() const { return false; }

  [[nodiscard]] teng::Result<void> save();

 private:
  void sync_status_from_document();

  EditorMode mode_{EditorMode::Edit};
  EditorSelection selection_;
  EditorDocumentController document_controller_;
  std::string last_status_;
};

}  // namespace teng::editor
