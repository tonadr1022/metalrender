#include "GeneratedSceneAssetsSmokeTest.hpp"

#include <algorithm>
#include <filesystem>

#include "engine/render/RenderScene.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/scene/SceneAssetLoader.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::engine {
namespace {

[[nodiscard]] std::filesystem::path find_repo_root() {
  std::filesystem::path path = std::filesystem::current_path();
  while (path.has_parent_path()) {
    if (std::filesystem::exists(path / "resources" / "project.toml")) {
      return path;
    }
    path = path.parent_path();
  }
  return {};
}

[[nodiscard]] bool load_and_check_scene(const std::filesystem::path& path, size_t mesh_count) {
  SceneManager scenes;
  Result<SceneAssetLoadResult> loaded = load_scene_asset(scenes, path);
  if (!loaded || !(*loaded).scene || scenes.active_scene() != (*loaded).scene) {
    return false;
  }
  if (!(*loaded).scene->tick(1.f / 60.f)) {
    return false;
  }

  const RenderScene render_scene = extract_render_scene(*(*loaded).scene);
  if (render_scene.cameras.size() != 1 || render_scene.directional_lights.size() != 1 ||
      render_scene.meshes.size() != mesh_count) {
    return false;
  }
  return std::ranges::all_of(render_scene.meshes, [](const RenderMesh& mesh) {
    return mesh.entity.is_valid() && mesh.model.is_valid();
  });
}

}  // namespace

bool run_generated_scene_assets_smoke_test() {
  const std::filesystem::path root = find_repo_root();
  if (root.empty()) {
    return false;
  }

  return load_and_check_scene(root / "resources/scenes/demo_00_cube.tscene.toml", 1) &&
         load_and_check_scene(root / "resources/scenes/demo_01_cube_grid.tscene.toml", 81);
}

}  // namespace teng::engine
