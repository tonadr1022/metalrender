#include "DemoSceneAuthoringSmokeTest.hpp"

#include <vector>

#include "DemoSceneEcsBridge.hpp"
#include "ScenePresets.hpp"
#include "engine/render/RenderScene.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::gfx::demo_scene_compat {

bool run_demo_scene_authoring_smoke_test() {
  demo_scenes::seed_demo_scene_rng(10000000);
  std::vector<demo_scenes::DemoScenePresetData> presets;
  append_default_scene_preset_data(presets, {});
  if (presets.size() < 3 || presets[0].models.empty() || presets[2].models.empty()) {
    return false;
  }

  engine::SceneManager scenes;
  const DemoSceneEntityGuids grid_guids = apply_demo_preset_to_scene(scenes, presets[2], {});
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
  if (!resolve_model_path(grid_render_scene.meshes.front().model).has_value()) {
    return false;
  }

  const engine::EntityGuid old_mesh_guid = grid_render_scene.meshes.back().entity;
  const DemoSceneEntityGuids suzanne_guids = apply_demo_preset_to_scene(scenes, presets[0], {});
  if (suzanne_guids.camera != grid_guids.camera || suzanne_guids.light != grid_guids.light ||
      scene->has_entity(old_mesh_guid)) {
    return false;
  }
  if (!scene->tick(1.f / 60.f)) {
    return false;
  }

  const engine::RenderScene suzanne_render_scene = engine::extract_render_scene(*scene);
  return suzanne_render_scene.cameras.size() == 1 &&
         suzanne_render_scene.directional_lights.size() == 1 &&
         suzanne_render_scene.meshes.size() == 1 &&
         suzanne_render_scene.meshes.front().model.is_valid() &&
         resolve_model_path(suzanne_render_scene.meshes.front().model).has_value();
}

}  // namespace teng::gfx::demo_scene_compat
