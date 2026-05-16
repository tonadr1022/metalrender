#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

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

  [[nodiscard]] bool can_save(EditorMode mode) const;
  [[nodiscard]] teng::Result<void> save(EditorMode mode);

 private:
  std::unique_ptr<teng::engine::scene::authoring::SceneDocument> document_;
  teng::engine::SceneId edit_scene_id_;
  std::string last_status_;
};

}  // namespace teng::editor
