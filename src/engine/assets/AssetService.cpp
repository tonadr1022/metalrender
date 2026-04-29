#include "engine/assets/AssetService.hpp"

#include <glm/mat4x4.hpp>
#include <utility>

namespace teng::engine::assets {

AssetService::AssetService(AssetServiceConfig config)
    : config_(std::move(config)),
      database_(AssetDatabaseConfig{
          .project_root = config_.project_root,
          .content_root = config_.content_root,
      }) {}

ModelAssetLoadResult AssetService::load_model(AssetId id) {
  if (const auto it = model_assets_.find(id); it != model_assets_.end()) {
    return {.status = AssetLoadStatus::Ok, .asset = it->second.get()};
  }

  const AssetRecord* record{};
  const AssetLoadStatus status = validate_model_asset(id, record);
  if (status != AssetLoadStatus::Ok) {
    return {.status = status};
  }

  auto asset = std::make_unique<ModelAsset>();
  asset->id = id;
  asset->source_path = record->source_path;
  if (!gfx::load_model(absolute_source_path(record->source_path), glm::mat4{1}, asset->model,
                       asset->load_result)) {
    return {.status = AssetLoadStatus::ImportFailed};
  }

  const ModelAsset* loaded = asset.get();
  model_assets_.emplace(id, std::move(asset));
  return {.status = AssetLoadStatus::Ok, .asset = loaded};
}

ModelAssetImportResult AssetService::import_model_for_upload(AssetId id) {
  const AssetRecord* record{};
  const AssetLoadStatus status = validate_model_asset(id, record);
  if (status != AssetLoadStatus::Ok) {
    return {.status = status};
  }

  auto asset = std::make_unique<ModelAssetImport>();
  asset->id = id;
  asset->source_path = record->source_path;
  if (!gfx::load_model(absolute_source_path(record->source_path), glm::mat4{1}, asset->model,
                       asset->load_result)) {
    return {.status = AssetLoadStatus::ImportFailed};
  }

  return {.status = AssetLoadStatus::Ok, .asset = std::move(asset)};
}

AssetLoadStatus AssetService::validate_model_asset(AssetId id, const AssetRecord*& out_record) const {
  const AssetRecord* record = database_.find(id);
  if (!record || record->status == AssetRecordStatus::Tombstoned) {
    return AssetLoadStatus::MissingAsset;
  }
  if (record->type.value != "model") {
    return AssetLoadStatus::WrongType;
  }
  if (record->status == AssetRecordStatus::MissingSource ||
      !std::filesystem::is_regular_file(absolute_source_path(record->source_path))) {
    return AssetLoadStatus::MissingSource;
  }
  out_record = record;
  return AssetLoadStatus::Ok;
}

std::filesystem::path AssetService::absolute_source_path(
    const std::filesystem::path& source_path) const {
  return (config_.project_root / config_.content_root / source_path).lexically_normal();
}

const char* to_string(AssetLoadStatus status) {
  switch (status) {
    case AssetLoadStatus::Ok:
      return "ok";
    case AssetLoadStatus::MissingAsset:
      return "missing-asset";
    case AssetLoadStatus::WrongType:
      return "wrong-type";
    case AssetLoadStatus::MissingSource:
      return "missing-source";
    case AssetLoadStatus::ImportFailed:
      return "import-failed";
  }
  return "unknown";
}

}  // namespace teng::engine::assets
