#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/assets/AssetRegistry.hpp"

namespace teng::engine::assets {

struct AssetDatabaseConfig {
  std::filesystem::path project_root;
  std::filesystem::path content_root{"resources"};
  std::string sidecar_extension{".tasset.toml"};
  std::filesystem::path aggregate_cache_path{"resources/local/asset_cache/assets.registry.toml"};
};

enum class AssetDiagnosticKind : uint8_t {
  DuplicateAssetId,
  MissingSource,
  MissingMetadata,
  StaleSourceHash,
  SourcePathMismatch,
  TomlParseError,
  SchemaError,
  BrokenReference,
  RedirectCycle,
  UnresolvedRedirect,
};

struct AssetDiagnostic {
  AssetDiagnosticKind kind{};
  std::filesystem::path path;
  AssetId asset{};
  AssetId referenced_asset{};
  std::string message;
};

struct AssetScanReport {
  std::vector<AssetDiagnostic> diagnostics;

  [[nodiscard]] bool has_errors() const { return !diagnostics.empty(); }
  [[nodiscard]] size_t count(AssetDiagnosticKind kind) const;
};

struct AssetRegisterSourceDesc {
  std::filesystem::path source_path;
  AssetTypeId type;
  std::string display_name;
  std::string importer;
  uint32_t importer_version{1};
  std::vector<AssetDependency> dependencies;
  std::vector<std::string> labels;
  std::optional<AssetId> id;
};

struct AssetDeleteResult {
  bool deleted{};
  std::vector<AssetDependency> blocking_dependents;
  std::vector<AssetDiagnostic> diagnostics;
};

struct AssetRedirectFixupReport {
  std::vector<AssetDiagnostic> diagnostics;
  size_t collapsed_redirects{};
};

class AssetDatabase {
 public:
  explicit AssetDatabase(AssetDatabaseConfig config);

  [[nodiscard]] const AssetDatabaseConfig& config() const { return config_; }
  [[nodiscard]] const AssetRegistry& registry() const { return registry_; }

  [[nodiscard]] AssetScanReport scan();
  [[nodiscard]] AssetRegistryResult register_source(const AssetRegisterSourceDesc& desc,
                                                    AssetId* out_id = nullptr);
  [[nodiscard]] AssetRegistryResult move_asset(AssetId id, const std::filesystem::path& new_path);
  [[nodiscard]] AssetDeleteResult delete_asset(AssetId id, bool force = false);
  [[nodiscard]] AssetRedirectFixupReport fixup_redirects();

  [[nodiscard]] const AssetRecord* find(AssetId id) const { return registry_.find(id); }
  [[nodiscard]] std::optional<std::filesystem::path> source_path(AssetId id) const;
  [[nodiscard]] std::optional<AssetId> asset_id_for_source_path(
      const std::filesystem::path& source_path) const;
  [[nodiscard]] std::optional<AssetId> resolve_path_redirect(
      const std::filesystem::path& source_path) const;

 private:
  [[nodiscard]] std::filesystem::path absolute_content_root() const;
  [[nodiscard]] std::filesystem::path absolute_cache_path() const;
  [[nodiscard]] std::filesystem::path absolute_source_path(
      const std::filesystem::path& source_path) const;
  [[nodiscard]] std::filesystem::path relative_source_path(
      const std::filesystem::path& source_path) const;
  [[nodiscard]] std::filesystem::path sidecar_path_for_source(
      const std::filesystem::path& source_path) const;
  [[nodiscard]] bool is_under_content_root(const std::filesystem::path& path) const;

  [[nodiscard]] std::optional<AssetRecord> load_sidecar(const std::filesystem::path& sidecar_path,
                                                        AssetScanReport& report) const;
  [[nodiscard]] bool save_sidecar(const AssetRecord& record,
                                  const std::vector<std::string>& labels = {}) const;
  void write_aggregate_index_best_effort() const;
  [[nodiscard]] std::string hash_source_file(const std::filesystem::path& source_path) const;
  void add_dependency_diagnostics(AssetScanReport& report) const;

  AssetDatabaseConfig config_;
  AssetRegistry registry_;
  std::unordered_map<std::string, AssetId> path_redirects_;
};

[[nodiscard]] std::string_view to_string(AssetDiagnosticKind kind);

}  // namespace teng::engine::assets
