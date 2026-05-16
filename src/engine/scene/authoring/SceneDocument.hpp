#pragma once

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string_view>
#include <utility>

#include "core/Result.hpp"
#include "engine/scene/SceneIds.hpp"

namespace teng::engine {

class Scene;
struct SceneSerializationContext;

namespace scene::authoring {

enum class SceneAuthoringMutation;

struct SceneDocumentOptions {
  std::optional<std::filesystem::path> path;
};

class SceneDocument {
 public:
  SceneDocument(Scene& scene, const SceneSerializationContext& serialization,
                SceneDocumentOptions options = {});

  [[nodiscard]] Scene& scene() { return scene_; }
  [[nodiscard]] const Scene& scene() const { return scene_; }
  [[nodiscard]] const SceneSerializationContext& serialization() const { return serialization_; }

  [[nodiscard]] bool dirty() const { return revision_ != saved_revision_; }
  [[nodiscard]] const std::optional<std::filesystem::path>& path() const { return path_; }
  void set_path(std::filesystem::path path) { path_ = std::move(path); }
  [[nodiscard]] uint64_t revision() const { return revision_; }
  [[nodiscard]] uint64_t saved_revision() const { return saved_revision_; }

  [[nodiscard]] Result<EntityGuid> create_entity(std::string_view name = {});
  [[nodiscard]] Result<void> rename_entity(EntityGuid entity, std::string_view name);
  [[nodiscard]] Result<void> destroy_entity(EntityGuid entity);
  [[nodiscard]] Result<void> add_component(EntityGuid entity, std::string_view component_key);
  [[nodiscard]] Result<void> remove_component(EntityGuid entity, std::string_view component_key);
  [[nodiscard]] Result<void> set_component(EntityGuid entity, std::string_view component_key,
                                           const nlohmann::json& payload);
  [[nodiscard]] Result<void> edit_component_field(EntityGuid entity, std::string_view component_key,
                                                  std::string_view field_key,
                                                  const nlohmann::json& draft_value);
  [[nodiscard]] Result<void> save();
  [[nodiscard]] Result<void> save_as(std::filesystem::path path);

 private:
  friend void mark_committed(SceneDocument& document, SceneAuthoringMutation mutation);
  friend void mark_saved(SceneDocument& document);

  void mark_committed_for_authoring();
  void mark_saved_for_authoring();

  Scene& scene_;
  const SceneSerializationContext& serialization_;
  std::optional<std::filesystem::path> path_;
  uint64_t revision_{};
  uint64_t saved_revision_{};
};

}  // namespace scene::authoring
}  // namespace teng::engine
