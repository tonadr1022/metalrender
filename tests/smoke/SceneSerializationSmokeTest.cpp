#include "SceneSerializationSmokeTest.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string_view>
#include <system_error>

#include "TestHelpers.hpp"
#include "engine/render/RenderScene.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneManager.hpp"
#include "engine/scene/SceneSerialization.hpp"

namespace teng::engine {
namespace {

using json = nlohmann::json;

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
  return R"scene({
  "scene_format_version": 2,
  "schema": {
    "required_modules": [
      {
        "id": "teng.core",
        "version": 1
      }
    ],
    "required_components": {
      "teng.core.camera": 1,
      "teng.core.directional_light": 1,
      "teng.core.mesh_renderable": 1,
      "teng.core.sprite_renderable": 1,
      "teng.core.transform": 1
    }
  },
  "scene": {
    "name": "serialization smoke"
  },
  "entities": [
    {
      "guid": "00000000000003e9",
      "name": "camera",
      "components": {
        "teng.core.camera": {
          "fov_y": 1.04719755,
          "z_near": 0.1,
          "z_far": 10000.0,
          "primary": true
        },
        "teng.core.transform": {
          "translation": [0.0, 0.0, 3.0],
          "rotation": [1.0, 0.0, 0.0, 0.0],
          "scale": [1.0, 1.0, 1.0]
        }
      }
    },
    {
      "guid": "00000000000003ea",
      "name": "light",
      "components": {
        "teng.core.directional_light": {
          "direction": [0.35, 1.0, 0.4],
          "color": [1.0, 1.0, 1.0],
          "intensity": 1.0
        },
        "teng.core.transform": {
          "translation": [0.0, 0.0, 0.0],
          "rotation": [1.0, 0.0, 0.0, 0.0],
          "scale": [1.0, 1.0, 1.0]
        }
      }
    },
    {
      "guid": "00000000000003eb",
      "name": "mesh",
      "components": {
        "teng.core.mesh_renderable": {
          "model": "12345678-9abc-def0-0000-000000000001"
        },
        "teng.core.sprite_renderable": {
          "texture": "12345678-9abc-def0-0000-000000000002",
          "tint": [1.0, 0.5, 0.25, 1.0],
          "sorting_layer": 1,
          "sorting_order": 2
        },
        "teng.core.transform": {
          "translation": [2.0, 0.0, 0.0],
          "rotation": [1.0, 0.0, 0.0, 0.0],
          "scale": [1.0, 1.0, 1.0]
        }
      }
    }
  ]
})scene"
         "\n";
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

  const SceneTestContexts test_contexts = make_scene_test_contexts();

  SceneManager scenes(test_contexts.flecs_components);
  Result<SceneLoadResult> loaded =
      load_scene_file(scenes, test_contexts.scene_serialization, valid_path);
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
  const auto* const camera = scene.find_entity(EntityGuid{1001}).try_get<Camera>();
  if (!camera || !camera->primary) {
    return false;
  }

  const std::filesystem::path round_trip_path = root / "saved_v2.tscene.json";
  if (!save_scene_file(scene, test_contexts.scene_serialization, round_trip_path)) {
    return false;
  }
  std::ifstream in(round_trip_path);
  if (!in) {
    return false;
  }
  const std::string saved_text(std::istreambuf_iterator<char>{in},
                               std::istreambuf_iterator<char>{});
  json saved_json;
  try {
    saved_json = json::parse(saved_text);
  } catch (const json::parse_error&) {
    return false;
  }
  if (!validate_scene_file_full_report(test_contexts.scene_serialization, saved_json).has_value()) {
    return false;
  }
  if (saved_json.value("scene_format_version", 0) != 2) {
    return false;
  }
  if (!saved_json.contains("schema") || !saved_json["schema"].contains("required_components")) {
    return false;
  }
  const auto& req = saved_json["schema"]["required_components"];
  if (!req.contains("teng.core.transform") || !req.contains("teng.core.camera")) {
    return false;
  }
  if (!saved_json["entities"].is_array() || saved_json["entities"].size() != 3) {
    return false;
  }
  if (!saved_json["entities"][0]["guid"].is_string()) {
    return false;
  }
  SceneManager round_trip_scenes(test_contexts.flecs_components);
  Result<SceneLoadResult> round_trip_loaded =
      load_scene_file(round_trip_scenes, test_contexts.scene_serialization, round_trip_path);
  if (!round_trip_loaded || !(*round_trip_loaded).scene) {
    return false;
  }

  const std::filesystem::path invalid_path = root / "invalid.tscene.json";
  if (!write_text_file(invalid_path,
                       "{ \"scene_format_version\": 2, \"schema\": { \"required_modules\": [], "
                       "\"required_components\": {} }, \"scene\": { \"name\": \"invalid\" }, "
                       "\"entities\": [ { \"guid\": \"0000000000000001\", \"components\": { "
                       "\"teng.core.local_to_world\": {} } } ] }\n")) {
    return false;
  }
  SceneManager invalid_scenes(test_contexts.flecs_components);
  if (load_scene_file(invalid_scenes, test_contexts.scene_serialization, invalid_path) ||
      invalid_scenes.active_scene()) {
    return false;
  }

  std::filesystem::remove_all(root, ec);
  return true;
}

}  // namespace teng::engine
