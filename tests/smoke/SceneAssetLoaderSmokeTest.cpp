#include "SceneAssetLoaderSmokeTest.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

#include "engine/render/RenderScene.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/scene/SceneAssetLoader.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::engine {
namespace {

[[nodiscard]] AssetId test_model_id() {
  return AssetId::from_parts(0x123456789abcdef0ull, 0x0000000000000001ull);
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

[[nodiscard]] std::string valid_scene_text() {
  return "schema_version = 1\n"
         "name = \"loader smoke\"\n"
         "\n"
         "[[entities]]\n"
         "guid = 1001\n"
         "name = \"camera\"\n"
         "[entities.transform]\n"
         "translation = [0.0, 0.0, 3.0]\n"
         "rotation = [1.0, 0.0, 0.0, 0.0]\n"
         "scale = [1.0, 1.0, 1.0]\n"
         "[entities.local_to_world]\n"
         "matrix = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 3.0, "
         "1.0]\n"
         "[entities.camera]\n"
         "fov_y = 1.04719755\n"
         "z_near = 0.1\n"
         "z_far = 10000.0\n"
         "primary = true\n"
         "\n"
         "[[entities]]\n"
         "guid = 1002\n"
         "name = \"light\"\n"
         "[entities.transform]\n"
         "translation = [0.0, 0.0, 0.0]\n"
         "rotation = [1.0, 0.0, 0.0, 0.0]\n"
         "scale = [1.0, 1.0, 1.0]\n"
         "[entities.directional_light]\n"
         "direction = [0.35, 1.0, 0.4]\n"
         "color = [1.0, 1.0, 1.0]\n"
         "intensity = 1.0\n"
         "\n"
         "[[entities]]\n"
         "guid = 1003\n"
         "name = \"mesh\"\n"
         "[entities.transform]\n"
         "translation = [0.0, 0.0, 0.0]\n"
         "rotation = [1.0, 0.0, 0.0, 0.0]\n"
         "scale = [1.0, 1.0, 1.0]\n"
         "[entities.mesh_renderable]\n"
         "model = \"" +
         test_model_id().to_string() + "\"\n";
}

}  // namespace

bool run_scene_asset_loader_smoke_test() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "metalrender_scene_asset_loader_smoke";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const std::filesystem::path valid_path = root / "valid.tscene.toml";
  if (!write_text_file(valid_path, valid_scene_text())) {
    return false;
  }

  SceneManager scenes;
  Result<SceneAssetLoadResult> loaded = load_scene_asset(scenes, valid_path);
  if (!loaded || !(*loaded).scene || scenes.active_scene() != (*loaded).scene ||
      (*loaded).scene->name() != "loader smoke") {
    return false;
  }

  Scene& scene = *(*loaded).scene;
  if (!scene.has_entity(EntityGuid{1001}) || !scene.has_entity(EntityGuid{1002}) ||
      !scene.has_entity(EntityGuid{1003})) {
    return false;
  }

  if (!scene.tick(1.f / 60.f)) {
    return false;
  }

  const RenderScene render_scene = extract_render_scene(scene);
  if (render_scene.cameras.size() != 1 || render_scene.directional_lights.size() != 1 ||
      render_scene.meshes.size() != 1) {
    return false;
  }
  if (render_scene.cameras.front().entity != EntityGuid{1001} ||
      render_scene.directional_lights.front().entity != EntityGuid{1002} ||
      render_scene.meshes.front().entity != EntityGuid{1003} ||
      render_scene.meshes.front().model != test_model_id()) {
    return false;
  }

  const std::filesystem::path invalid_path = root / "invalid.tscene.toml";
  if (!write_text_file(invalid_path,
                       "schema_version = 1\n"
                       "name = \"invalid\"\n"
                       "entities = [\"not an entity table\"]\n")) {
    return false;
  }

  SceneManager invalid_scenes;
  if (load_scene_asset(invalid_scenes, invalid_path)) {
    return false;
  }

  std::filesystem::remove_all(root, ec);
  return true;
}

}  // namespace teng::engine
