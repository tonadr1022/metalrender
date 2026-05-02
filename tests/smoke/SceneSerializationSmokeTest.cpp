#include "SceneSerializationSmokeTest.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

#include "engine/render/RenderScene.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneManager.hpp"
#include "engine/scene/SceneSerialization.hpp"

namespace teng::engine {
namespace {

[[nodiscard]] AssetId test_model_id() {
  return AssetId::from_parts(0x123456789abcdef0ull, 0x0000000000000001ull);
}

[[nodiscard]] AssetId test_texture_id() {
  return AssetId::from_parts(0x123456789abcdef0ull, 0x0000000000000002ull);
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
  return "{\n"
         "  \"entities\": [\n"
         "    {\n"
         "      \"components\": {\n"
         "        \"camera\": { \"fov_y\": 1.04719755, \"primary\": true, \"z_far\": 10000.0, \"z_near\": 0.1 },\n"
         "        \"transform\": { \"rotation\": [1.0, 0.0, 0.0, 0.0], \"scale\": [1.0, 1.0, 1.0], \"translation\": [0.0, 0.0, 3.0] }\n"
         "      },\n"
         "      \"guid\": 1001,\n"
         "      \"name\": \"camera\"\n"
         "    },\n"
         "    {\n"
         "      \"components\": {\n"
         "        \"directional_light\": { \"color\": [1.0, 1.0, 1.0], \"direction\": [0.35, 1.0, 0.4], \"intensity\": 1.0 },\n"
         "        \"transform\": { \"rotation\": [1.0, 0.0, 0.0, 0.0], \"scale\": [1.0, 1.0, 1.0], \"translation\": [0.0, 0.0, 0.0] }\n"
         "      },\n"
         "      \"guid\": 1002,\n"
         "      \"name\": \"light\"\n"
         "    },\n"
         "    {\n"
         "      \"components\": {\n"
         "        \"mesh_renderable\": { \"model\": \"" +
         test_model_id().to_string() +
         "\" },\n"
         "        \"sprite_renderable\": { \"sorting_layer\": 1, \"sorting_order\": 2, \"texture\": \"" +
         test_texture_id().to_string() +
         "\", \"tint\": [1.0, 0.5, 0.25, 1.0] },\n"
         "        \"transform\": { \"rotation\": [1.0, 0.0, 0.0, 0.0], \"scale\": [1.0, 1.0, 1.0], \"translation\": [2.0, 0.0, 0.0] }\n"
         "      },\n"
         "      \"guid\": 1003,\n"
         "      \"name\": \"mesh\"\n"
         "    }\n"
         "  ],\n"
         "  \"registry_version\": 1,\n"
         "  \"scene\": { \"name\": \"serialization smoke\" }\n"
         "}\n";
}

[[nodiscard]] bool approx(float a, float b) { return std::abs(a - b) < 0.0001f; }

}  // namespace

bool run_scene_serialization_smoke_test() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "metalrender_scene_serialization_smoke";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);

  const std::filesystem::path valid_path = root / "valid.tscene.json";
  if (!write_text_file(valid_path, valid_scene_text())) {
    return false;
  }

  SceneManager scenes;
  Result<SceneLoadResult> loaded = load_scene_file(scenes, valid_path);
  if (!loaded || !(*loaded).scene || scenes.active_scene() != (*loaded).scene ||
      (*loaded).scene->name() != "serialization smoke") {
    return false;
  }

  Scene& scene = *(*loaded).scene;
  if (!scene.has_entity(EntityGuid{1001}) || !scene.has_entity(EntityGuid{1002}) ||
      !scene.has_entity(EntityGuid{1003})) {
    return false;
  }

  const LocalToWorld* mesh_ltw = scene.get_local_to_world(EntityGuid{1003});
  if (!mesh_ltw || !approx((*mesh_ltw).value[3][0], 2.f)) {
    return false;
  }

  const RenderScene render_scene = extract_render_scene(scene);
  if (render_scene.cameras.size() != 1 || render_scene.directional_lights.size() != 1 ||
      render_scene.meshes.size() != 1 || render_scene.sprites.size() != 1) {
    return false;
  }
  if (render_scene.cameras.front().entity != EntityGuid{1001} ||
      render_scene.directional_lights.front().entity != EntityGuid{1002} ||
      render_scene.meshes.front().entity != EntityGuid{1003} ||
      render_scene.meshes.front().model != test_model_id() ||
      render_scene.sprites.front().texture != test_texture_id()) {
    return false;
  }

  const std::filesystem::path round_trip_path = root / "round_trip.tscene.json";
  if (!save_scene_file(scene, round_trip_path)) {
    return false;
  }
  SceneManager round_trip_scenes;
  Result<SceneLoadResult> round_trip_loaded = load_scene_file(round_trip_scenes, round_trip_path);
  if (!round_trip_loaded || !(*round_trip_loaded).scene ||
      !(*round_trip_loaded).scene->has_entity(EntityGuid{1003})) {
    return false;
  }

  const std::filesystem::path cooked_path = root / "valid.tscene.bin";
  const std::filesystem::path dumped_path = root / "dumped.tscene.json";
  if (!cook_scene_file(valid_path, cooked_path) || !dump_cooked_scene_file(cooked_path, dumped_path) ||
      !validate_scene_file(dumped_path)) {
    return false;
  }

  const std::filesystem::path invalid_path = root / "invalid.tscene.json";
  if (!write_text_file(invalid_path,
                       "{ \"registry_version\": 1, \"scene\": { \"name\": \"invalid\" }, "
                       "\"entities\": [ { \"guid\": 1, \"components\": { \"local_to_world\": {} } } ] }\n")) {
    return false;
  }
  if (validate_scene_file(invalid_path)) {
    return false;
  }

  std::filesystem::remove_all(root, ec);
  return true;
}

}  // namespace teng::engine
