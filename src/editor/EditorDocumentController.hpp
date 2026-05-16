#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Result.hpp"
#include "engine/scene/SceneIds.hpp"
#include "engine/scene/authoring/SceneDocument.hpp"

namespace teng::engine {
class SceneManager;
struct SceneSerializationContext;
}  // namespace teng::engine

namespace teng::editor {

enum class EditorMode {
  Edit,
  Play,
};

[[nodiscard]] const char* to_string(EditorMode mode);

struct EditorOperationRecord {
  std::string label;
  std::vector<teng::engine::EntityGuid> affected_entities;
  std::string coalescing_key;
  bool succeeded{};
  std::string status;
};

class EditorDocumentController {
 public:
  EditorDocumentController() = default;
  EditorDocumentController(const EditorDocumentController&) = delete;
  EditorDocumentController& operator=(const EditorDocumentController&) = delete;
  EditorDocumentController(EditorDocumentController&&) noexcept = default;
  EditorDocumentController& operator=(EditorDocumentController&&) noexcept = default;
  ~EditorDocumentController();

  [[nodiscard]] teng::Result<void> bind(
      teng::engine::SceneManager& scenes,
      const teng::engine::SceneSerializationContext& serialization,
      teng::engine::SceneId edit_scene_id,
      std::optional<std::filesystem::path> path = std::nullopt);

  [[nodiscard]] bool bound() const { return document_ != nullptr; }
  [[nodiscard]] teng::engine::scene::authoring::SceneDocument& document();
  [[nodiscard]] const teng::engine::scene::authoring::SceneDocument& document() const;
  [[nodiscard]] teng::engine::SceneId edit_scene_id() const { return edit_scene_id_; }
  [[nodiscard]] const std::optional<std::filesystem::path>& path() const;
  [[nodiscard]] bool dirty() const;
  [[nodiscard]] uint64_t revision() const;
  [[nodiscard]] uint64_t saved_revision() const;
  [[nodiscard]] const std::string& last_status() const { return last_status_; }
  [[nodiscard]] const std::optional<EditorOperationRecord>& last_operation() const {
    return last_operation_;
  }

  [[nodiscard]] bool can_save(EditorMode mode) const;
  [[nodiscard]] bool can_create_entity(EditorMode mode) const;
  [[nodiscard]] bool can_delete_entity(
      EditorMode mode, const std::optional<teng::engine::EntityGuid>& entity) const;
  [[nodiscard]] teng::Result<void> save(EditorMode mode);
  [[nodiscard]] teng::Result<teng::engine::EntityGuid> create_entity(
      EditorMode mode, std::string_view label = "Entity");
  [[nodiscard]] teng::Result<void> delete_entity(EditorMode mode, teng::engine::EntityGuid entity);

 private:
  void record_operation(EditorOperationRecord operation);

  std::unique_ptr<teng::engine::scene::authoring::SceneDocument> document_;
  teng::engine::SceneId edit_scene_id_;
  std::string last_status_;
  std::optional<EditorOperationRecord> last_operation_;
};

}  // namespace teng::editor
