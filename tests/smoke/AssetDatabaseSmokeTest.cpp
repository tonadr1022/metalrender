#include "AssetDatabaseSmokeTest.hpp"

#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

#include "engine/assets/AssetDatabase.hpp"
#include "engine/assets/AssetRegistry.hpp"

namespace teng::engine::assets {

namespace {

[[nodiscard]] AssetId test_id(uint64_t low) {
  return AssetId::from_parts(0xfedcba9876543210ull, low);
}

[[nodiscard]] bool write_text_file(const std::filesystem::path& path, std::string_view text) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path);
  if (!out) {
    return false;
  }
  out << text;
  return true;
}

[[nodiscard]] AssetDatabase make_test_database(const std::filesystem::path& root) {
  return AssetDatabase(AssetDatabaseConfig{
      .project_root = root,
      .content_root = "resources",
      .aggregate_cache_path = "resources/local/asset_cache/assets.registry.toml"});
}

[[nodiscard]] bool prepare_asset_database_smoke_root(const std::filesystem::path& root) {
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "resources/textures", ec);
  std::filesystem::create_directories(root / "resources/materials", ec);

  return write_text_file(root / "resources/textures/albedo.txt", "albedo") &&
         write_text_file(root / "resources/materials/mat.txt", "material");
}

[[nodiscard]] bool run_asset_registration_roundtrip_smoke(const std::filesystem::path& root,
                                                          AssetId& texture_id) {
  AssetDatabase db = make_test_database(root);
  if (db.register_source(AssetRegisterSourceDesc{.source_path = "textures/albedo.txt",
                                                 .type = {.value = "texture"},
                                                 .display_name = "Albedo",
                                                 .importer = "text",
                                                 .id = test_id(1)},
                         &texture_id) != AssetRegistryResult::Ok ||
      texture_id != test_id(1)) {
    return false;
  }
  if (!std::filesystem::exists(root / "resources/textures/albedo.txt.tasset.toml") ||
      !std::filesystem::exists(root / "resources/local/asset_cache/assets.registry.toml")) {
    return false;
  }

  if (!write_text_file(root / "resources/textures/register_fail.txt", "fail")) {
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directory(root / "resources/textures/register_fail.txt.tasset.toml", ec);
  AssetDatabase register_fail_db = make_test_database(root);
  const AssetId register_fail_id = test_id(7);
  if (register_fail_db.register_source(
          AssetRegisterSourceDesc{.source_path = "textures/register_fail.txt",
                                  .type = {.value = "texture"},
                                  .display_name = "RegisterFail",
                                  .importer = "text",
                                  .id = register_fail_id}) != AssetRegistryResult::MissingAsset ||
      register_fail_db.find(register_fail_id) != nullptr) {
    return false;
  }
  std::filesystem::remove(root / "resources/textures/register_fail.txt.tasset.toml", ec);
  std::filesystem::remove(root / "resources/textures/register_fail.txt", ec);

  AssetDatabase roundtrip = make_test_database(root);
  const AssetScanReport report = roundtrip.scan();
  return report.count(AssetDiagnosticKind::MissingMetadata) == 1 &&
         roundtrip.asset_id_for_source_path("textures/albedo.txt") == texture_id;
}

[[nodiscard]] bool run_asset_delete_dependency_smoke(const std::filesystem::path& root,
                                                     AssetId texture_id) {
  AssetDatabase roundtrip = make_test_database(root);
  (void)roundtrip.scan();
  if (roundtrip.register_source(AssetRegisterSourceDesc{
          .source_path = "materials/mat.txt",
          .type = {.value = "material"},
          .display_name = "Material",
          .importer = "text",
          .dependencies = {{.asset = texture_id, .kind = AssetDependencyKind::Strong}},
          .id = test_id(2)}) != AssetRegistryResult::Ok) {
    return false;
  }
  if (roundtrip.delete_asset(texture_id).deleted) {
    return false;
  }
  const AssetDeleteResult forced_delete = roundtrip.delete_asset(texture_id, true);
  if (!forced_delete.deleted || forced_delete.diagnostics.empty()) {
    return false;
  }
  const AssetScanReport tombstone_report = make_test_database(root).scan();
  return tombstone_report.count(AssetDiagnosticKind::BrokenReference) != 0;
}

[[nodiscard]] bool run_asset_move_redirect_smoke(const std::filesystem::path& root) {
  AssetDatabase move_db = make_test_database(root);
  if (!write_text_file(root / "resources/textures/move.txt", "move")) {
    return false;
  }
  AssetId move_id;
  if (move_db.register_source(AssetRegisterSourceDesc{.source_path = "textures/move.txt",
                                                      .type = {.value = "texture"},
                                                      .display_name = "Move",
                                                      .importer = "text",
                                                      .id = test_id(3)},
                              &move_id) != AssetRegistryResult::Ok) {
    return false;
  }
  if (move_db.move_asset(move_id, "textures/moved.txt") != AssetRegistryResult::Ok ||
      move_db.asset_id_for_source_path("textures/moved.txt") != move_id ||
      move_db.asset_id_for_source_path("textures/move.txt") != move_id) {
    return false;
  }
  const AssetRedirectFixupReport fixup_report = move_db.fixup_redirects();
  return fixup_report.collapsed_redirects == 1 &&
         !move_db.asset_id_for_source_path("textures/move.txt").has_value();
}

[[nodiscard]] bool run_asset_move_rollback_smoke(const std::filesystem::path& root) {
  AssetDatabase rollback_db = make_test_database(root);
  if (!write_text_file(root / "resources/textures/rollback.txt", "rollback")) {
    return false;
  }
  AssetId rollback_id;
  if (rollback_db.register_source(AssetRegisterSourceDesc{.source_path = "textures/rollback.txt",
                                                          .type = {.value = "texture"},
                                                          .display_name = "Rollback",
                                                          .importer = "text",
                                                          .id = test_id(8)},
                                  &rollback_id) != AssetRegistryResult::Ok) {
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directory(root / "resources/textures/rollback_moved.txt.tasset.toml", ec);
  if (rollback_db.move_asset(rollback_id, "textures/rollback_moved.txt") !=
          AssetRegistryResult::MissingAsset ||
      rollback_db.source_path(rollback_id) != std::filesystem::path("textures/rollback.txt") ||
      !std::filesystem::exists(root / "resources/textures/rollback.txt") ||
      std::filesystem::exists(root / "resources/textures/rollback_moved.txt")) {
    return false;
  }
  std::filesystem::remove(root / "resources/textures/rollback_moved.txt.tasset.toml", ec);
  return true;
}

[[nodiscard]] bool run_asset_scan_diagnostic_smoke(const std::filesystem::path& root) {
  AssetDatabase stale_db = make_test_database(root);
  if (!write_text_file(root / "resources/textures/stale.txt", "old")) {
    return false;
  }
  if (stale_db.register_source(AssetRegisterSourceDesc{.source_path = "textures/stale.txt",
                                                       .type = {.value = "texture"},
                                                       .display_name = "Stale",
                                                       .importer = "text",
                                                       .id = test_id(4)}) !=
      AssetRegistryResult::Ok) {
    return false;
  }
  if (!write_text_file(root / "resources/textures/stale.txt", "new")) {
    return false;
  }
  if (make_test_database(root).scan().count(AssetDiagnosticKind::StaleSourceHash) == 0) {
    return false;
  }

  AssetDatabase missing_source_db = make_test_database(root);
  if (!write_text_file(root / "resources/textures/missing_source.txt", "source")) {
    return false;
  }
  if (missing_source_db.register_source(
          AssetRegisterSourceDesc{.source_path = "textures/missing_source.txt",
                                  .type = {.value = "texture"},
                                  .display_name = "MissingSource",
                                  .importer = "text",
                                  .id = test_id(6)}) != AssetRegistryResult::Ok) {
    return false;
  }
  std::error_code ec;
  std::filesystem::remove(root / "resources/textures/missing_source.txt", ec);
  if (make_test_database(root).scan().count(AssetDiagnosticKind::MissingSource) == 0) {
    return false;
  }

  if (!write_text_file(root / "resources/textures/invalid_status.txt", "invalid-status") ||
      !write_text_file(root / "resources/textures/invalid_status.txt.tasset.toml",
                       "schema_version = 1\n"
                       "id = \"fedcba98-7654-3210-0000-000000000009\"\n"
                       "type = \"texture\"\n"
                       "source_path = \"textures/invalid_status.txt\"\n"
                       "display_name = \"InvalidStatus\"\n"
                       "importer = \"text\"\n"
                       "importer_version = 1\n"
                       "source_content_hash = \"fnv1a64:0000000000000001\"\n"
                       "status = \"bogus\"\n")) {
    return false;
  }
  AssetDatabase invalid_status_db = make_test_database(root);
  const AssetScanReport invalid_status_report = invalid_status_db.scan();
  if (invalid_status_report.count(AssetDiagnosticKind::SchemaError) == 0 ||
      invalid_status_db.find(test_id(9)) != nullptr) {
    return false;
  }

  if (!write_text_file(root / "resources/textures/dup_a.txt", "a") ||
      !write_text_file(root / "resources/textures/dup_b.txt", "b")) {
    return false;
  }
  AssetDatabase dup_db = make_test_database(root);
  if (dup_db.register_source(AssetRegisterSourceDesc{.source_path = "textures/dup_a.txt",
                                                     .type = {.value = "texture"},
                                                     .display_name = "DupA",
                                                     .importer = "text",
                                                     .id = test_id(5)}) !=
      AssetRegistryResult::Ok) {
    return false;
  }
  const std::filesystem::path dup_sidecar = root / "resources/textures/dup_b.txt.tasset.toml";
  const std::filesystem::path original_sidecar = root / "resources/textures/dup_a.txt.tasset.toml";
  std::filesystem::copy_file(original_sidecar, dup_sidecar,
                             std::filesystem::copy_options::overwrite_existing, ec);
  std::string dup_contents;
  {
    const std::ifstream in(dup_sidecar);
    std::ostringstream ss;
    ss << in.rdbuf();
    dup_contents = ss.str();
  }
  const size_t path_pos = dup_contents.find("textures/dup_a.txt");
  if (path_pos == std::string::npos) {
    return false;
  }
  dup_contents.replace(path_pos, std::string("textures/dup_a.txt").size(), "textures/dup_b.txt");
  if (!write_text_file(dup_sidecar, dup_contents)) {
    return false;
  }
  if (make_test_database(root).scan().count(AssetDiagnosticKind::DuplicateAssetId) == 0) {
    return false;
  }
  return true;
}

[[nodiscard]] bool run_asset_redirect_cycle_smoke() {
  AssetRegistry redirects;
  return redirects.add_redirect(test_id(10), test_id(11)) == AssetRegistryResult::Ok &&
         redirects.add_redirect(test_id(11), test_id(10)) == AssetRegistryResult::RedirectCycle;
}

}  // namespace

bool run_asset_database_smoke_test() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "metalrender_asset_database_smoke";

  AssetId texture_id;
  if (!prepare_asset_database_smoke_root(root) ||
      !run_asset_registration_roundtrip_smoke(root, texture_id) ||
      !run_asset_delete_dependency_smoke(root, texture_id) ||
      !run_asset_move_redirect_smoke(root) || !run_asset_move_rollback_smoke(root) ||
      !run_asset_scan_diagnostic_smoke(root) || !run_asset_redirect_cycle_smoke()) {
    return false;
  }

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  return true;
}

}  // namespace teng::engine::assets
