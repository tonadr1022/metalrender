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

namespace {

[[nodiscard]] AssetId test_id(uint64_t low) {
  return AssetId::from_parts(0x123456789abcdef0ull, low);
}

[[nodiscard]] bool expect_result(AssetRegistryResult actual, AssetRegistryResult expected) {
  return actual == expected;
}

}  // namespace

bool run_asset_registry_smoke_test() {
  const std::optional<AssetId> parsed = AssetId::parse("12345678-9abc-def0-0000-000000000001");
  if (!parsed || *parsed != test_id(1) ||
      parsed->to_string() != "12345678-9abc-def0-0000-000000000001") {
    return false;
  }
  if (AssetId::parse("00000000-0000-0000-0000-000000000000").has_value()) {
    return false;
  }

  AssetRegistry registry;
  const AssetId texture_id = test_id(2);
  const AssetId material_id = test_id(3);
  const AssetId model_id = test_id(4);
  const AssetId scene_id = test_id(5);

  if (!expect_result(registry.add_record(AssetRecord{
                         .id = texture_id,
                         .type = {.value = "texture"},
                         .source_path = "textures/albedo.png",
                         .display_name = "albedo",
                         .importer = "texture",
                         .importer_version = 1,
                     }),
                     AssetRegistryResult::Ok)) {
    return false;
  }
  if (!expect_result(registry.add_record(AssetRecord{
                         .id = texture_id,
                         .type = {.value = "texture"},
                     }),
                     AssetRegistryResult::DuplicateAssetId)) {
    return false;
  }
  if (!expect_result(
          registry.add_record(AssetRecord{
              .id = material_id,
              .type = {.value = "material"},
              .dependencies = {{.asset = texture_id, .kind = AssetDependencyKind::Strong}},
          }),
          AssetRegistryResult::Ok)) {
    return false;
  }
  if (!expect_result(
          registry.add_record(AssetRecord{
              .id = model_id,
              .type = {.value = "model"},
              .dependencies = {{.asset = material_id, .kind = AssetDependencyKind::Strong}},
          }),
          AssetRegistryResult::Ok)) {
    return false;
  }
  if (!expect_result(registry.add_record(AssetRecord{
                         .id = scene_id,
                         .type = {.value = "scene"},
                         .dependencies = {{.asset = model_id, .kind = AssetDependencyKind::Strong},
                                          {.asset = texture_id, .kind = AssetDependencyKind::Soft}},
                     }),
                     AssetRegistryResult::Ok)) {
    return false;
  }

  const std::vector<AssetDependency> texture_dependents = registry.dependents(texture_id);
  if (texture_dependents.size() != 2 || texture_dependents[0].asset != material_id ||
      texture_dependents[0].kind != AssetDependencyKind::Strong ||
      texture_dependents[1].asset != scene_id ||
      texture_dependents[1].kind != AssetDependencyKind::Soft) {
    return false;
  }

  if (!expect_result(registry.add_redirect(test_id(20), model_id), AssetRegistryResult::Ok)) {
    return false;
  }
  if (registry.resolve_redirect(test_id(20)) != model_id) {
    return false;
  }
  if (!expect_result(registry.add_redirect(model_id, test_id(20)),
                     AssetRegistryResult::RedirectCycle)) {
    return false;
  }
  if (!expect_result(registry.add_redirect(scene_id, scene_id),
                     AssetRegistryResult::SelfRedirect)) {
    return false;
  }
  if (!expect_result(registry.mark_tombstoned(texture_id), AssetRegistryResult::Ok)) {
    return false;
  }
  const AssetRecord* texture = registry.find(texture_id);
  return texture && texture->status == AssetRecordStatus::Tombstoned && !texture->is_available() &&
         to_string(AssetDependencyKind::Generated) == "generated" &&
         to_string(AssetRecordStatus::MissingSource) == "missing-source" &&
         to_string(AssetRegistryResult::RedirectCycle) == "redirect-cycle";
}

}  // namespace teng::engine::assets
