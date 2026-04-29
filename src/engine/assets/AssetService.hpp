#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>

#include "engine/assets/AssetDatabase.hpp"
#include "gfx/ModelInstance.hpp"
#include "gfx/ModelLoader.hpp"

namespace teng::engine::assets {

struct AssetServiceConfig {
  std::filesystem::path project_root;
  std::filesystem::path content_root{"resources"};
};

enum class AssetLoadStatus : uint8_t {
  Ok,
  MissingAsset,
  WrongType,
  MissingSource,
  ImportFailed,
};

struct ModelAsset {
  AssetId id;
  std::filesystem::path source_path;
  ModelInstance model;
  gfx::ModelLoadResult load_result;
};

struct ModelAssetLoadResult {
  AssetLoadStatus status{AssetLoadStatus::MissingAsset};
  const ModelAsset* asset{};
};

struct ModelAssetImport {
  AssetId id;
  std::filesystem::path source_path;
  ModelInstance model;
  gfx::ModelLoadResult load_result;
};

struct ModelAssetImportResult {
  AssetLoadStatus status{AssetLoadStatus::MissingAsset};
  std::unique_ptr<ModelAssetImport> asset;
};

class AssetService {
 public:
  explicit AssetService(AssetServiceConfig config);

  [[nodiscard]] AssetDatabase& database() { return database_; }
  [[nodiscard]] const AssetDatabase& database() const { return database_; }
  [[nodiscard]] AssetScanReport scan() { return database_.scan(); }
  [[nodiscard]] ModelAssetLoadResult load_model(AssetId id);
  [[nodiscard]] ModelAssetImportResult import_model_for_upload(AssetId id);

 private:
  [[nodiscard]] AssetLoadStatus validate_model_asset(AssetId id, const AssetRecord*& out_record) const;
  [[nodiscard]] std::filesystem::path absolute_source_path(
      const std::filesystem::path& source_path) const;

  AssetServiceConfig config_;
  AssetDatabase database_;
  std::unordered_map<AssetId, std::unique_ptr<ModelAsset>> model_assets_;
};

[[nodiscard]] const char* to_string(AssetLoadStatus status);

}  // namespace teng::engine::assets
