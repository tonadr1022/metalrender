#include "DemoSceneAuthoringSmokeTest.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

#include "DemoSceneEcsBridge.hpp"
#include "ScenePresets.hpp"
#include "engine/assets/AssetDatabase.hpp"
#include "engine/render/RenderScene.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::gfx::demo_scene_compat {

namespace {

[[nodiscard]] engine::AssetId test_id(uint64_t low) {
  return engine::AssetId::from_parts(0x123456789abcdeffull, low);
}

[[nodiscard]] bool write_text_file(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path);
  if (!out) {
    return false;
  }
  out << "demo model fixture";
  return true;
}

[[nodiscard]] bool register_demo_model(engine::assets::AssetDatabase& database,
                                       const std::filesystem::path& resource_dir,
                                       const std::string& preset_path, uint64_t id_low) {
  const std::filesystem::path resolved = demo_scenes::resolve_model_path(resource_dir, preset_path);
  const std::filesystem::path relative = resolved.lexically_relative(resource_dir).lexically_normal();
  if (!write_text_file(resolved)) {
    return false;
  }
  return database.register_source(engine::assets::AssetRegisterSourceDesc{
             .source_path = relative,
             .type = {.value = "model"},
             .display_name = preset_path,
             .importer = "gltf",
             .id = test_id(id_low),
         }) == engine::assets::AssetRegistryResult::Ok;
}

}  // namespace

bool run_demo_scene_authoring_smoke_test() {
  demo_scenes::seed_demo_scene_rng(10000000);
  std::vector<demo_scenes::DemoScenePresetData> presets;
  append_default_scene_preset_data(presets, {});
  if (presets.size() < 3 || presets[0].models.empty() || presets[2].models.empty()) {
    return false;
  }

  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "metalrender_demo_scene_authoring_smoke";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const std::filesystem::path resource_dir = root / "resources";
  engine::assets::AssetDatabase assets(engine::assets::AssetDatabaseConfig{
      .project_root = root,
      .content_root = "resources",
  });
  if (!register_demo_model(assets, resource_dir, demo_scenes::k_suzanne_path, 1) ||
      !register_demo_model(assets, resource_dir, demo_scenes::k_sponza_path, 2)) {
    return false;
  }

  engine::assets::AssetDatabase scanned_assets(engine::assets::AssetDatabaseConfig{
      .project_root = root,
      .content_root = "resources",
  });
  if (scanned_assets.scan().count(engine::assets::AssetDiagnosticKind::SchemaError) != 0 ||
      !scanned_assets.asset_id_for_source_path("models/gltf/Models/Suzanne/glTF_ktx2/Suzanne.gltf")
           .has_value()) {
    return false;
  }

  engine::SceneManager scenes;
  const DemoSceneEntityGuids grid_guids =
      apply_demo_preset_to_scene(scenes, presets[2], resource_dir, scanned_assets);
  engine::Scene* scene = scenes.active_scene();
  if (!scene || !scene->has_entity(grid_guids.camera) || !scene->has_entity(grid_guids.light)) {
    return false;
  }
  if (!scene->tick(1.f / 60.f)) {
    return false;
  }

  engine::RenderScene grid_render_scene = engine::extract_render_scene(*scene);
  if (grid_render_scene.cameras.size() != 1 || grid_render_scene.directional_lights.size() != 1 ||
      grid_render_scene.meshes.size() <= 1 || !grid_render_scene.meshes.front().model.is_valid()) {
    return false;
  }
  const engine::AssetId first_model = grid_render_scene.meshes.front().model;
  size_t first_model_count = 0;
  for (const engine::RenderMesh& mesh : grid_render_scene.meshes) {
    if (mesh.model == first_model) {
      ++first_model_count;
    }
  }
  if (first_model_count <= 1) {
    return false;
  }
  const engine::EntityGuid old_mesh_guid = grid_render_scene.meshes.back().entity;
  const DemoSceneEntityGuids suzanne_guids =
      apply_demo_preset_to_scene(scenes, presets[0], resource_dir, scanned_assets);
  if (suzanne_guids.camera != grid_guids.camera || suzanne_guids.light != grid_guids.light ||
      scene->has_entity(old_mesh_guid)) {
    return false;
  }
  if (!scene->tick(1.f / 60.f)) {
    return false;
  }

  const engine::RenderScene suzanne_render_scene = engine::extract_render_scene(*scene);
  const bool ok = suzanne_render_scene.cameras.size() == 1 &&
                  suzanne_render_scene.directional_lights.size() == 1 &&
                  suzanne_render_scene.meshes.size() == 1 &&
                  suzanne_render_scene.meshes.front().model.is_valid();
  std::filesystem::remove_all(root, ec);
  return ok;
}

}  // namespace teng::gfx::demo_scene_compat
