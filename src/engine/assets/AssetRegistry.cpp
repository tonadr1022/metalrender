#include "engine/assets/AssetRegistry.hpp"

#include <algorithm>
#include <utility>

namespace teng::engine::assets {

AssetRegistryResult AssetRegistry::add_record(AssetRecord record) {
  if (!record.id.is_valid()) {
    return AssetRegistryResult::InvalidAssetId;
  }
  if (!record.type.is_valid()) {
    return AssetRegistryResult::InvalidAssetType;
  }
  if (records_.contains(record.id)) {
    return AssetRegistryResult::DuplicateAssetId;
  }
  records_.emplace(record.id, std::move(record));
  return AssetRegistryResult::Ok;
}

AssetRegistryResult AssetRegistry::add_redirect(AssetId from, AssetId to) {
  if (!from.is_valid() || !to.is_valid()) {
    return AssetRegistryResult::InvalidAssetId;
  }
  if (from == to) {
    return AssetRegistryResult::SelfRedirect;
  }
  if (redirect_path_contains(to, from)) {
    return AssetRegistryResult::RedirectCycle;
  }
  redirects_[from] = to;
  return AssetRegistryResult::Ok;
}

AssetRegistryResult AssetRegistry::mark_tombstoned(AssetId id) {
  AssetRecord* record = find(id);
  if (!record) {
    return AssetRegistryResult::MissingAsset;
  }
  record->status = AssetRecordStatus::Tombstoned;
  return AssetRegistryResult::Ok;
}

const AssetRecord* AssetRegistry::find(AssetId id) const {
  const auto it = records_.find(id);
  return it == records_.end() ? nullptr : &it->second;
}

AssetRecord* AssetRegistry::find(AssetId id) {
  const auto it = records_.find(id);
  return it == records_.end() ? nullptr : &it->second;
}

std::optional<AssetId> AssetRegistry::resolve_redirect(AssetId id) const {
  if (!id.is_valid()) {
    return std::nullopt;
  }

  AssetId current = id;
  for (size_t depth = 0; depth <= redirects_.size(); ++depth) {
    const auto it = redirects_.find(current);
    if (it == redirects_.end()) {
      return current;
    }
    current = it->second;
  }
  return std::nullopt;
}

std::vector<AssetDependency> AssetRegistry::dependencies(AssetId id) const {
  const AssetRecord* record = find(id);
  if (!record) {
    return {};
  }
  return record->dependencies;
}

std::vector<AssetDependency> AssetRegistry::dependents(AssetId id) const {
  std::vector<AssetDependency> result;
  for (const auto& [record_id, record] : records_) {
    for (const AssetDependency& dependency : record.dependencies) {
      if (dependency.asset == id) {
        result.push_back(AssetDependency{.asset = record_id, .kind = dependency.kind});
      }
    }
  }
  std::ranges::sort(result, [](const AssetDependency& a, const AssetDependency& b) {
    if (a.asset != b.asset) {
      return a.asset < b.asset;
    }
    return static_cast<uint8_t>(a.kind) < static_cast<uint8_t>(b.kind);
  });
  return result;
}

std::vector<AssetId> AssetRegistry::records() const {
  std::vector<AssetId> result;
  result.reserve(records_.size());
  for (const auto& [id, record] : records_) {
    (void)record;
    result.push_back(id);
  }
  std::ranges::sort(result, [](AssetId a, AssetId b) { return a < b; });
  return result;
}

std::optional<std::filesystem::path> AssetRegistry::source_path(AssetId id) const {
  const AssetRecord* record = find(id);
  if (!record) {
    return std::nullopt;
  }
  return record->source_path;
}

std::optional<AssetId> AssetRegistry::asset_id_for_source_path(
    const std::filesystem::path& source_path) const {
  const std::filesystem::path normalized = source_path.lexically_normal();
  for (const auto& [id, record] : records_) {
    if (record.source_path.lexically_normal() == normalized) {
      return id;
    }
  }
  return std::nullopt;
}

bool AssetRegistry::redirect_path_contains(AssetId start, AssetId target) const {
  AssetId current = start;
  for (size_t depth = 0; depth <= redirects_.size(); ++depth) {
    if (current == target) {
      return true;
    }
    const auto it = redirects_.find(current);
    if (it == redirects_.end()) {
      return false;
    }
    current = it->second;
  }
  return current == target;
}

std::string_view to_string(AssetDependencyKind kind) {
  switch (kind) {
    case AssetDependencyKind::Strong:
      return "strong";
    case AssetDependencyKind::Soft:
      return "soft";
    case AssetDependencyKind::Generated:
      return "generated";
    case AssetDependencyKind::Tooling:
      return "tooling";
  }
  return "unknown";
}

std::string_view to_string(AssetRecordStatus status) {
  switch (status) {
    case AssetRecordStatus::Available:
      return "available";
    case AssetRecordStatus::MissingSource:
      return "missing-source";
    case AssetRecordStatus::MissingMetadata:
      return "missing-metadata";
    case AssetRecordStatus::Tombstoned:
      return "tombstoned";
  }
  return "unknown";
}

std::string_view to_string(AssetRegistryResult result) {
  switch (result) {
    case AssetRegistryResult::Ok:
      return "ok";
    case AssetRegistryResult::InvalidAssetId:
      return "invalid-asset-id";
    case AssetRegistryResult::InvalidAssetType:
      return "invalid-asset-type";
    case AssetRegistryResult::DuplicateAssetId:
      return "duplicate-asset-id";
    case AssetRegistryResult::MissingAsset:
      return "missing-asset";
    case AssetRegistryResult::RedirectCycle:
      return "redirect-cycle";
    case AssetRegistryResult::SelfRedirect:
      return "self-redirect";
    case AssetRegistryResult::Tombstoned:
      return "tombstoned";
  }
  return "unknown";
}

}  // namespace teng::engine::assets
