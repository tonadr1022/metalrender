#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "TestExtensionComponent.hpp"
#include "TestHelpers.hpp"
#include "engine/content/CookedArtifact.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneCooked.hpp"
#include "engine/scene/SceneManager.hpp"
#include "engine/scene/SceneSerialization.hpp"

namespace teng::engine {
namespace {

using json = nlohmann::json;

[[nodiscard]] AssetId asset_id(uint64_t low) {
  return AssetId::from_parts(0x123456789abcdef0ull, low);
}

[[nodiscard]] json core_scene_json() {
  const std::string model = asset_id(1).to_string();
  const std::string texture = asset_id(2).to_string();
  json scene = json::parse(R"json(
{
  "scene_format_version": 2,
  "schema": {
    "required_modules": [
      { "id": "teng.core", "version": 1 }
    ],
    "required_components": {
      "teng.core.camera": 1,
      "teng.core.directional_light": 1,
      "teng.core.mesh_renderable": 1,
      "teng.core.sprite_renderable": 1,
      "teng.core.transform": 1
    }
  },
  "scene": { "name": "cooked core test" },
  "entities": [
    {
      "guid": "0000000000000010",
      "name": "camera",
      "components": {
        "teng.core.transform": {
          "translation": [0.0, 1.0, 5.0],
          "rotation": [1.0, 0.0, 0.0, 0.0],
          "scale": [1.0, 1.0, 1.0]
        },
        "teng.core.camera": {
          "fov_y": 1.0,
          "z_near": 0.25,
          "z_far": 1000.0,
          "primary": true
        },
        "teng.core.directional_light": {
          "direction": [0.0, -1.0, 0.0],
          "color": [1.0, 0.75, 0.5],
          "intensity": 3.5
        }
      }
    },
    {
      "guid": "0000000000000020",
      "name": "renderables",
      "components": {
        "teng.core.transform": {
          "translation": [2.0, 0.0, 0.0],
          "rotation": [1.0, 0.0, 0.0, 0.0],
          "scale": [1.0, 2.0, 1.0]
        },
        "teng.core.mesh_renderable": { "model": "" },
        "teng.core.sprite_renderable": {
          "texture": "",
          "tint": [1.0, 0.5, 0.25, 1.0],
          "sorting_layer": 2,
          "sorting_order": 7
        }
      }
    }
  ]
}
)json");
  scene["entities"][1]["components"]["teng.core.mesh_renderable"]["model"] = model;
  scene["entities"][1]["components"]["teng.core.sprite_renderable"]["texture"] = texture;
  return scene;
}

[[nodiscard]] json extension_scene_json() {
  return json{
      {"scene_format_version", 2},
      {"schema",
       json{{"required_modules",
             json::array({json{{"id", std::string{k_test_extension_module_id}}, {"version", 1}}})},
            {"required_components", json{{std::string{k_test_extension_component_key}, 1}}}}},
      {"scene", json{{"name", "cooked extension test"}}},
      {"entities",
       json::array({json{{"guid", "0000000000000030"},
                         {"name", "extension"},
                         {"components", json{{std::string{k_test_extension_component_key},
                                              json{{"health", 42.5},
                                                   {"active", false},
                                                   {"kind", 1},
                                                   {"attachment", asset_id(3).to_string()}}}}}}})}};
}

void patch_u32(std::vector<std::byte>& bytes, size_t offset, uint32_t value) {
  REQUIRE(offset + sizeof(value) <= bytes.size());
  for (size_t i = 0; i < sizeof(value); ++i) {
    bytes[offset + i] = static_cast<std::byte>((value >> (i * 8u)) & 0xffu);
  }
}

void patch_u64(std::vector<std::byte>& bytes, size_t offset, uint64_t value) {
  REQUIRE(offset + sizeof(value) <= bytes.size());
  for (size_t i = 0; i < sizeof(value); ++i) {
    bytes[offset + i] = static_cast<std::byte>((value >> (i * 8u)) & 0xffu);
  }
}

[[nodiscard]] uint32_t read_u32(const std::vector<std::byte>& bytes, size_t offset) {
  REQUIRE(offset + sizeof(uint32_t) <= bytes.size());
  uint32_t value{};
  for (size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + i])) << (i * 8u);
  }
  return value;
}

[[nodiscard]] uint64_t read_u64(const std::vector<std::byte>& bytes, size_t offset) {
  REQUIRE(offset + sizeof(uint64_t) <= bytes.size());
  uint64_t value{};
  for (size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<uint64_t>(static_cast<uint8_t>(bytes[offset + i])) << (i * 8u);
  }
  return value;
}

[[nodiscard]] size_t section_payload_offset(const std::vector<std::byte>& bytes,
                                            uint32_t section_id) {
  constexpr size_t header_size =
      content::k_cooked_artifact_magic_size + content::k_cooked_artifact_kind_size +
      sizeof(uint32_t) * 4 +
      content::k_cooked_artifact_header_reserved_u32_count * sizeof(uint32_t);
  const uint32_t section_count =
      read_u32(bytes, content::k_cooked_artifact_magic_size + content::k_cooked_artifact_kind_size +
                          sizeof(uint32_t) * 3);
  for (uint32_t i = 0; i < section_count; ++i) {
    const size_t record = header_size + i * (sizeof(uint32_t) + sizeof(uint64_t) * 2);
    if (read_u32(bytes, record) == section_id) {
      return static_cast<size_t>(read_u64(bytes, record + sizeof(uint32_t)));
    }
  }
  FAIL("section not found");
  return 0;
}

}  // namespace

// NOLINTBEGIN(misc-use-anonymous-namespace)

TEST_CASE("cooked scene dumps canonical JSON and loads into ECS", "[scene_cooked]") {
  const SceneTestContexts contexts = make_scene_test_contexts();
  Result<nlohmann::ordered_json> canonical =
      canonicalize_scene_json(contexts.scene_serialization, core_scene_json());
  REQUIRE(canonical.has_value());
  Result<std::vector<std::byte>> cooked =
      cook_scene_to_memory(contexts.scene_serialization, core_scene_json());
  REQUIRE(cooked.has_value());

  Result<nlohmann::ordered_json> dumped =
      dump_cooked_scene_to_json(contexts.scene_serialization, *cooked);
  REQUIRE(dumped.has_value());
  CHECK(dumped->dump() == canonical->dump());

  SceneManager scenes(contexts.flecs_components);
  const Result<void> loaded =
      deserialize_cooked_scene(scenes, contexts.scene_serialization, *cooked);
  REQUIRE(loaded.has_value());
  Scene* scene = scenes.active_scene();
  REQUIRE(scene != nullptr);
  CHECK(scene->name() == "cooked core test");
  const flecs::entity renderables = scene->find_entity(EntityGuid{0x20});
  REQUIRE(renderables.is_valid());
  REQUIRE(renderables.has<MeshRenderable>());
  REQUIRE(renderables.has<SpriteRenderable>());
  CHECK(renderables.get<MeshRenderable>().model == asset_id(1));
  CHECK(renderables.get<SpriteRenderable>().texture == asset_id(2));
}

TEST_CASE("cooked scene loads from disk without JSON deserialization", "[scene_cooked]") {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "metalrender_scene_cooked_no_json_smoke";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const std::filesystem::path cooked_path = root / "scene.tcooked";

  const SceneTestContexts contexts = make_scene_test_contexts();
  Result<std::vector<std::byte>> cooked =
      cook_scene_to_memory(contexts.scene_serialization, core_scene_json());
  REQUIRE(cooked.has_value());

  std::filesystem::create_directories(root, ec);
  REQUIRE_FALSE(ec);
  {
    std::ofstream out(cooked_path, std::ios::binary | std::ios::trunc);
    REQUIRE(out);
    out.write(reinterpret_cast<const char*>(cooked->data()),
              static_cast<std::streamsize>(cooked->size()));
    REQUIRE(out);
  }

  SceneManager scenes(contexts.flecs_components);
  Scene& scene = scenes.create_scene("no json path");
  const Result<void> loaded =
      load_cooked_scene_file_no_json(scene, contexts.scene_serialization, cooked_path);
  REQUIRE(loaded.has_value());
  CHECK(scene.name() == "no json path");

  const flecs::entity renderables = scene.find_entity(EntityGuid{0x20});
  REQUIRE(renderables.is_valid());
  REQUIRE(renderables.has<MeshRenderable>());
  REQUIRE(renderables.has<SpriteRenderable>());
  REQUIRE(renderables.has<Transform>());
  const auto& transform = renderables.get<Transform>();
  CHECK(transform.translation.x == 2.0f);
  CHECK(transform.translation.y == 0.0f);
  CHECK(transform.translation.z == 0.0f);
  CHECK(transform.rotation.w == 1.0f);
  CHECK(transform.rotation.x == 0.0f);
  CHECK(transform.rotation.y == 0.0f);
  CHECK(transform.rotation.z == 0.0f);
  CHECK(transform.scale.x == 1.0f);
  CHECK(transform.scale.y == 2.0f);
  CHECK(transform.scale.z == 1.0f);
  CHECK(renderables.get<MeshRenderable>().model == asset_id(1));
  CHECK(renderables.get<SpriteRenderable>().texture == asset_id(2));

  const flecs::entity camera = scene.find_entity(EntityGuid{0x10});
  REQUIRE(camera.is_valid());
  REQUIRE(camera.has<Camera>());
  CHECK(camera.get<Camera>().fov_y == 1.0f);
  CHECK(camera.get<Camera>().z_near == 0.25f);
  CHECK(camera.get<Camera>().z_far == 1000.0f);

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("cooked scene supports extension components without central tables", "[scene_cooked]") {
  const SceneTestContexts contexts = make_scene_test_contexts_with_test_extension();
  Result<std::vector<std::byte>> cooked =
      cook_scene_to_memory(contexts.scene_serialization, extension_scene_json());
  REQUIRE(cooked.has_value());
  Result<nlohmann::ordered_json> dumped =
      dump_cooked_scene_to_json(contexts.scene_serialization, *cooked);
  REQUIRE(dumped.has_value());
  CHECK(
      (*dumped)["entities"][0]["components"][std::string{k_test_extension_component_key}]["kind"] ==
      1);

  SceneManager scenes(contexts.flecs_components);
  const Result<void> loaded =
      deserialize_cooked_scene(scenes, contexts.scene_serialization, *cooked);
  REQUIRE(loaded.has_value());
  const flecs::entity entity = scenes.active_scene()->find_entity(EntityGuid{0x30});
  REQUIRE(entity.is_valid());
  REQUIRE(entity.has<TestExtensionComponent>());
  CHECK(entity.get<TestExtensionComponent>().kind == TestExtensionKind::Beta);
  CHECK(entity.get<TestExtensionComponent>().attachment == asset_id(3));
}

TEST_CASE("cooked scene asset dependency extraction stays graph-friendly", "[scene_cooked]") {
  const SceneTestContexts core_contexts = make_scene_test_contexts();
  Result<nlohmann::ordered_json> canonical =
      canonicalize_scene_json(core_contexts.scene_serialization, core_scene_json());
  REQUIRE(canonical.has_value());
  Result<std::vector<SceneAssetDependency>> dependencies = collect_scene_asset_dependencies(
      core_contexts.scene_serialization, json::parse(canonical->dump()));
  REQUIRE(dependencies.has_value());
  REQUIRE(dependencies->size() == 2);
  CHECK(std::ranges::any_of(*dependencies, [](const SceneAssetDependency& dep) {
    return dep.asset == asset_id(1) && dep.component_key == "teng.core.mesh_renderable" &&
           dep.field_key == "model" && dep.entity == EntityGuid{0x20};
  }));
  CHECK(std::ranges::any_of(*dependencies, [](const SceneAssetDependency& dep) {
    return dep.asset == asset_id(2) && dep.component_key == "teng.core.sprite_renderable" &&
           dep.field_key == "texture" && dep.entity == EntityGuid{0x20};
  }));

  const SceneTestContexts extension_contexts = make_scene_test_contexts_with_test_extension();
  Result<nlohmann::ordered_json> extension_canonical =
      canonicalize_scene_json(extension_contexts.scene_serialization, extension_scene_json());
  REQUIRE(extension_canonical.has_value());
  Result<std::vector<SceneAssetDependency>> extension_dependencies =
      collect_scene_asset_dependencies(extension_contexts.scene_serialization,
                                       json::parse(extension_canonical->dump()));
  REQUIRE(extension_dependencies.has_value());
  REQUIRE(extension_dependencies->size() == 1);
  CHECK(extension_dependencies->front().asset == asset_id(3));
  CHECK(extension_dependencies->front().component_key == k_test_extension_component_key);
  CHECK(extension_dependencies->front().field_key == "attachment");
}

TEST_CASE("cooked scene path convention", "[scene_cooked]") {
  CHECK(is_cooked_scene_file_path(std::filesystem::path{"demo_01_cube_grid.tscene.bin"}));
  CHECK_FALSE(is_cooked_scene_file_path(std::filesystem::path{"demo_01_cube_grid.tscene.json"}));
  CHECK_FALSE(is_cooked_scene_file_path(std::filesystem::path{"scene.bin"}));
}

TEST_CASE("cooked scene rejects incompatible or corrupt bytes", "[scene_cooked]") {
  const SceneTestContexts contexts = make_scene_test_contexts();
  Result<std::vector<std::byte>> cooked =
      cook_scene_to_memory(contexts.scene_serialization, core_scene_json());
  REQUIRE(cooked.has_value());

  std::vector<std::byte> bad_version = *cooked;
  patch_u32(bad_version,
            content::k_cooked_artifact_magic_size + content::k_cooked_artifact_kind_size, 999);
  CHECK_FALSE(dump_cooked_scene_to_json(contexts.scene_serialization, bad_version).has_value());

  std::vector<std::byte> bad_kind = *cooked;
  bad_kind[content::k_cooked_artifact_magic_size] = static_cast<std::byte>('x');
  CHECK_FALSE(dump_cooked_scene_to_json(contexts.scene_serialization, bad_kind).has_value());

  std::vector<std::byte> truncated = *cooked;
  truncated.pop_back();
  CHECK_FALSE(dump_cooked_scene_to_json(contexts.scene_serialization, truncated).has_value());

  std::vector<std::byte> bad_component_id = *cooked;
  const size_t component_section = section_payload_offset(bad_component_id, 5);
  const size_t first_component_record = component_section + sizeof(uint32_t);
  patch_u64(bad_component_id, first_component_record, 42);
  CHECK_FALSE(
      dump_cooked_scene_to_json(contexts.scene_serialization, bad_component_id).has_value());

  std::vector<std::byte> bad_schema_version = *cooked;
  patch_u32(bad_schema_version, first_component_record + sizeof(uint64_t), 999);
  CHECK_FALSE(
      dump_cooked_scene_to_json(contexts.scene_serialization, bad_schema_version).has_value());
}

TEST_CASE("cooked scene rejects runtime-only component payloads before emission",
          "[scene_cooked]") {
  const SceneTestContexts contexts = make_scene_test_contexts();
  json scene = core_scene_json();
  scene["schema"]["required_components"]["teng.core.local_to_world"] = 1;
  scene["entities"][0]["components"]["teng.core.local_to_world"] =
      json{{"value", json::array({1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                                  0.0, 0.0, 1.0})}};
  CHECK_FALSE(cook_scene_to_memory(contexts.scene_serialization, scene).has_value());
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::engine
