#include "GeneratedSceneAssetsSmokeTest.hpp"

#include <algorithm>
#include <filesystem>

#include "TestHelpers.hpp"
#include "engine/render/RenderScene.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/scene/SceneManager.hpp"
#include "engine/scene/SceneSerialization.hpp"

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

[[nodiscard]] bool load_and_check_scene(const SceneComponentContext& component_ctx,
                                        const std::filesystem::path& path, size_t mesh_count) {
  SceneManager scenes(component_ctx);
  Result<SceneLoadResult> loaded = load_scene_file(scenes, path);
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

  SceneComponentContext component_ctx = make_scene_component_context();
  return load_and_check_scene(component_ctx, root / "resources/scenes/demo_00_cube.tscene.json",
                              1) &&
         load_and_check_scene(component_ctx,
                              root / "resources/scenes/demo_01_cube_grid.tscene.json", 81);
}

}  // namespace teng::engine
