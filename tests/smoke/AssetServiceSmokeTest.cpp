#include "AssetServiceSmokeTest.hpp"

#include <fstream>
#include <string_view>
#include <utility>

#include "engine/assets/AssetService.hpp"

namespace teng::engine::assets {

namespace {

[[nodiscard]] AssetId test_id(uint64_t low) {
  return AssetId::from_parts(0xabcddcba12344321ull, low);
}

[[nodiscard]] std::filesystem::path find_repo_resources_dir() {
  std::filesystem::path curr_path = std::filesystem::current_path();
  while (curr_path.has_parent_path()) {
    const std::filesystem::path candidate = curr_path / "resources";
    if (std::filesystem::exists(candidate / "models/Cube/glTF/Cube.gltf")) {
      return candidate;
    }
    const std::filesystem::path parent = curr_path.parent_path();
    if (parent == curr_path) {
      break;
    }
    curr_path = parent;
  }
  return {};
}

[[nodiscard]] bool copy_cube_model_fixture(const std::filesystem::path& root) {
  const std::filesystem::path src_dir = find_repo_resources_dir() / "models/Cube/glTF";
  const std::filesystem::path dst_dir = root / "resources/models/Cube/glTF";
  if (src_dir.empty() || !std::filesystem::exists(src_dir / "Cube.gltf")) {
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(dst_dir, ec);
  if (ec) {
    return false;
  }
  for (const char* filename : {"Cube.gltf", "Cube.bin", "Cube_BaseColor.png"}) {
    std::filesystem::copy_file(src_dir / filename, dst_dir / filename,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      return false;
    }
  }
  return true;
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

}  // namespace

bool run_asset_service_smoke_test() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "metalrender_asset_service_smoke";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  if (!copy_cube_model_fixture(root) ||
      !write_text_file(root / "resources/textures/not_model.txt", "texture")) {
    return false;
  }

  AssetService service(AssetServiceConfig{.project_root = root, .content_root = "resources"});

  AssetId model_id;
  if (service.database().register_source(
          AssetRegisterSourceDesc{.source_path = "models/Cube/glTF/Cube.gltf",
                                  .type = {.value = "model"},
                                  .display_name = "Cube",
                                  .importer = "gltf",
                                  .id = test_id(1)},
          &model_id) != AssetRegistryResult::Ok) {
    return false;
  }
  if (service.database().register_source(
          AssetRegisterSourceDesc{.source_path = "textures/not_model.txt",
                                  .type = {.value = "texture"},
                                  .display_name = "NotModel",
                                  .importer = "text",
                                  .id = test_id(2)}) != AssetRegistryResult::Ok) {
    return false;
  }

  AssetService scanned_service(
      AssetServiceConfig{.project_root = root, .content_root = "resources"});
  const AssetScanReport scan = scanned_service.scan();
  if (scan.count(AssetDiagnosticKind::SchemaError) != 0 ||
      scan.count(AssetDiagnosticKind::MissingSource) != 0 ||
      scan.count(AssetDiagnosticKind::DuplicateAssetId) != 0 ||
      !scanned_service.database().find(model_id) || !scanned_service.database().find(test_id(2))) {
    return false;
  }

  const ModelAssetLoadResult loaded = scanned_service.load_model(model_id);
  if (loaded.status != AssetLoadStatus::Ok || !loaded.asset ||
      loaded.asset->load_result.meshes.empty() || loaded.asset->model.nodes.empty()) {
    return false;
  }
  const ModelAssetLoadResult loaded_again = scanned_service.load_model(model_id);
  if (loaded_again.status != AssetLoadStatus::Ok || loaded_again.asset != loaded.asset) {
    return false;
  }
  ModelAssetImportResult imported = scanned_service.import_model_for_upload(model_id);
  if (imported.status != AssetLoadStatus::Ok || !imported.asset ||
      imported.asset->load_result.meshes.empty() || imported.asset->model.nodes.empty()) {
    return false;
  }
  if (scanned_service.load_model(test_id(2)).status != AssetLoadStatus::WrongType) {
    return false;
  }
  if (scanned_service.import_model_for_upload(test_id(2)).status != AssetLoadStatus::WrongType) {
    return false;
  }
  if (scanned_service.load_model(test_id(99)).status != AssetLoadStatus::MissingAsset) {
    return false;
  }

  std::filesystem::remove_all(root, ec);
  return true;
}

}  // namespace teng::engine::assets
