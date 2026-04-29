#include "AssetRegistrySmokeTest.hpp"

#include <optional>
#include <vector>

#include "engine/assets/AssetRegistry.hpp"

namespace teng::engine::assets {

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
