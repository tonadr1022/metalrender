#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

#include "core/Result.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/SceneIds.hpp"
#include "engine/scene/SceneManager.hpp"
#include "engine/scene/SceneSerialization.hpp"

namespace teng::engine {

struct SceneSerializationContext;

inline constexpr uint32_t k_cooked_scene_binary_format_version = 2;
inline constexpr uint32_t k_cooked_scene_json_format_version = 2;

struct SceneAssetDependency {
  AssetId asset;
  assets::AssetDependencyKind kind{assets::AssetDependencyKind::Strong};
  EntityGuid entity;
  std::string component_key;
  std::string field_key;
};

[[nodiscard]] Result<std::vector<std::byte>> cook_scene_to_memory(
    const SceneSerializationContext& serialization, const nlohmann::json& scene_json);
[[nodiscard]] Result<void> cook_scene_file(const SceneSerializationContext& serialization,
                                           const std::filesystem::path& input_path,
                                           const std::filesystem::path& output_path);
[[nodiscard]] Result<nlohmann::ordered_json> dump_cooked_scene_to_json(
    const SceneSerializationContext& serialization, std::span<const std::byte> bytes);
[[nodiscard]] Result<void> dump_cooked_scene_file(const SceneSerializationContext& serialization,
                                                  const std::filesystem::path& input_path,
                                                  const std::filesystem::path& output_path);
[[nodiscard]] Result<void> deserialize_cooked_scene(SceneManager& scenes,
                                                    const SceneSerializationContext& serialization,
                                                    std::span<const std::byte> bytes);
[[nodiscard]] Result<SceneLoadResult> load_cooked_scene_file(
    SceneManager& scenes, const SceneSerializationContext& serialization,
    const std::filesystem::path& path);
[[nodiscard]] Result<std::vector<SceneAssetDependency>> collect_scene_asset_dependencies(
    const SceneSerializationContext& serialization, const nlohmann::json& canonical_scene_json);

Result<void> load_cooked_scene_file_no_json(Scene& scene,
                                            const SceneSerializationContext& serialization,
                                            const std::filesystem::path& path);

}  // namespace teng::engine
