#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <span>
#include <vector>

#include "core/Result.hpp"
#include "engine/scene/SceneIds.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::core {
class DiagnosticReport;
}  // namespace teng::core

namespace teng::engine {

struct SceneSerializationContext;

inline constexpr int k_scene_registry_version = 1;
inline constexpr uint32_t k_scene_binary_format_version = 1;

struct SceneLoadResult {
  SceneId scene_id;
  Scene* scene{};
};

[[nodiscard]] Result<nlohmann::json> serialize_scene_to_json(const Scene& scene);
[[nodiscard]] Result<void> deserialize_scene_json(SceneManager& scenes, const nlohmann::json& json);
[[nodiscard]] Result<void> save_scene_file(const Scene& scene, const std::filesystem::path& path);
[[nodiscard]] Result<SceneLoadResult> load_scene_file(SceneManager& scenes,
                                                      const std::filesystem::path& path);
[[nodiscard]] Result<void> validate_scene_file(const std::filesystem::path& path);
[[nodiscard]] Result<void, core::DiagnosticReport> validate_scene_file(
    const SceneSerializationContext& serialization, const nlohmann::json& scene_json);

[[nodiscard]] Result<std::vector<std::byte>> cook_scene_to_memory(const nlohmann::json& json);
[[nodiscard]] Result<void> cook_scene_file(const std::filesystem::path& input_path,
                                           const std::filesystem::path& output_path);
[[nodiscard]] Result<nlohmann::json> dump_cooked_scene_to_json(std::span<const std::byte> bytes);
[[nodiscard]] Result<void> dump_cooked_scene_file(const std::filesystem::path& input_path,
                                                  const std::filesystem::path& output_path);
[[nodiscard]] Result<void> migrate_scene_file(const std::filesystem::path& input_path,
                                              const std::filesystem::path& output_path);

}  // namespace teng::engine
