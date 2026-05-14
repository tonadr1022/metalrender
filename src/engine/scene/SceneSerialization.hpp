#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

#include "core/Result.hpp"
#include "engine/scene/SceneIds.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::core {
class DiagnosticReport;
}  // namespace teng::core

namespace teng::engine {

struct SceneSerializationContext;

inline constexpr int k_scene_registry_version = 1;

struct SceneLoadResult {
  SceneId scene_id;
  Scene* scene{};
};

[[nodiscard]] Result<nlohmann::ordered_json> serialize_scene_to_json(
    const Scene& scene, const SceneSerializationContext& serialization);
[[nodiscard]] Result<void> deserialize_scene_json(SceneManager& scenes,
                                                  const SceneSerializationContext& serialization,
                                                  const nlohmann::json& json);
[[nodiscard]] Result<void> deserialize_scene_json2(SceneManager& scenes,
                                                   const SceneSerializationContext& serialization,
                                                   const nlohmann::json& json);
[[nodiscard]] Result<void> save_scene_file(const Scene& scene,
                                           const SceneSerializationContext& serialization,
                                           const std::filesystem::path& path);
[[nodiscard]] Result<SceneLoadResult> load_scene_file(
    SceneManager& scenes, const SceneSerializationContext& serialization,
    const std::filesystem::path& path);
[[nodiscard]] Result<void> validate_scene_file(const SceneSerializationContext& serialization,
                                               const std::filesystem::path& path);
[[nodiscard]] Result<void, core::DiagnosticReport> validate_scene_file_full_report(
    const SceneSerializationContext& serialization, const nlohmann::json& scene_json);
[[nodiscard]] Result<nlohmann::ordered_json> canonicalize_scene_json(
    const SceneSerializationContext& serialization, const nlohmann::json& scene_json);

[[nodiscard]] Result<void> migrate_scene_file(const std::filesystem::path& input_path,
                                              const std::filesystem::path& output_path);

}  // namespace teng::engine
