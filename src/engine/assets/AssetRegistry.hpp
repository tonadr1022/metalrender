#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "engine/scene/SceneIds.hpp"

namespace teng::engine::assets {

struct AssetTypeId {
  std::string value;

  [[nodiscard]] bool is_valid() const { return !value.empty(); }
  friend bool operator==(const AssetTypeId& a, const AssetTypeId& b) { return a.value == b.value; }
};

enum class AssetDependencyKind : uint8_t {
  Strong,
  Soft,
  Generated,
  Tooling,
};

enum class AssetRecordStatus : uint8_t {
  Available,
  MissingSource,
  MissingMetadata,
  Tombstoned,
};

enum class AssetRegistryResult : uint8_t {
  Ok,
  InvalidAssetId,
  InvalidAssetType,
  DuplicateAssetId,
  MissingAsset,
  RedirectCycle,
  SelfRedirect,
  Tombstoned,
};

struct AssetDependency {
  AssetId asset;
  AssetDependencyKind kind{AssetDependencyKind::Strong};
};

struct AssetRecord {
  AssetId id;
  AssetTypeId type;
  std::filesystem::path source_path;
  std::string display_name;
  std::string importer;
  uint32_t importer_version{};
  std::string source_content_hash;
  std::string imported_artifact_hash;
  std::vector<AssetDependency> dependencies;
  AssetRecordStatus status{AssetRecordStatus::Available};

  [[nodiscard]] bool is_available() const { return status == AssetRecordStatus::Available; }
};

class AssetRegistry {
 public:
  [[nodiscard]] AssetRegistryResult add_record(AssetRecord record);
  [[nodiscard]] AssetRegistryResult add_redirect(AssetId from, AssetId to);
  [[nodiscard]] AssetRegistryResult mark_tombstoned(AssetId id);

  [[nodiscard]] const AssetRecord* find(AssetId id) const;
  [[nodiscard]] AssetRecord* find(AssetId id);
  [[nodiscard]] bool contains(AssetId id) const { return find(id) != nullptr; }

  [[nodiscard]] std::optional<AssetId> resolve_redirect(AssetId id) const;
  [[nodiscard]] std::vector<AssetDependency> dependencies(AssetId id) const;
  [[nodiscard]] std::vector<AssetDependency> dependents(AssetId id) const;
  [[nodiscard]] std::vector<AssetId> records() const;
  [[nodiscard]] std::optional<std::filesystem::path> source_path(AssetId id) const;
  [[nodiscard]] std::optional<AssetId> asset_id_for_source_path(
      const std::filesystem::path& source_path) const;
  [[nodiscard]] const std::unordered_map<AssetId, AssetId>& redirects() const { return redirects_; }

 private:
  [[nodiscard]] bool redirect_path_contains(AssetId start, AssetId target) const;

  std::unordered_map<AssetId, AssetRecord> records_;
  std::unordered_map<AssetId, AssetId> redirects_;
};

[[nodiscard]] std::string_view to_string(AssetDependencyKind kind);
[[nodiscard]] std::string_view to_string(AssetRecordStatus status);
[[nodiscard]] std::string_view to_string(AssetRegistryResult result);

}  // namespace teng::engine::assets
