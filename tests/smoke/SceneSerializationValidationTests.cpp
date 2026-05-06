#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "TestHelpers.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneManager.hpp"
#include "engine/scene/SceneSerialization.hpp"

namespace teng::engine {
namespace {

using json = nlohmann::json;

[[nodiscard]] std::string test_asset_id_text() {
  return AssetId::from_parts(0x123456789abcdef0ull, 0x0000000000000001ull).to_string();
}

[[nodiscard]] json valid_scene_v2() {
  return json{
      {"scene_format_version", 2},
      {"schema",
       json{{"required_modules", json::array({json{{"id", "teng.core"}, {"version", 1}}})},
            {"required_components",
             json{{"teng.core.mesh_renderable", 1}, {"teng.core.transform", 1}}}}},
      {"scene", json{{"name", "validation test"}}},
      {"entities",
       json::array(
           {json{{"guid", "0000000000000001"},
                 {"name", "mesh"},
                 {"components",
                  json{{"teng.core.transform", json{{"translation", json::array({0.0, 0.0, 0.0})},
                                                    {"rotation", json::array({1.0, 0.0, 0.0, 0.0})},
                                                    {"scale", json::array({1.0, 1.0, 1.0})}}},
                       {"teng.core.mesh_renderable", json{{"model", test_asset_id_text()}}}}}}})}};
}

[[nodiscard]] bool validate(const json& scene_json) {
  const SceneTestContexts contexts = make_scene_test_contexts();
  return validate_scene_file(contexts.scene_serialization, scene_json).has_value();
}

}  // namespace

// NOLINTBEGIN(misc-use-anonymous-namespace)

TEST_CASE("JSON v2 scene validation accepts schema-backed scene documents",
          "[scene_serialization]") {
  CHECK(validate(valid_scene_v2()));

  SECTION("empty component maps are allowed") {
    json scene_json = valid_scene_v2();
    scene_json["schema"]["required_modules"] = json::array();
    scene_json["schema"]["required_components"] = json::object();
    scene_json["entities"] =
        json::array({json{{"guid", "0000000000000001"}, {"components", json::object()}}});
    CHECK(validate(scene_json));
  }
}

TEST_CASE("JSON v2 scene validation rejects invalid envelope shapes", "[scene_serialization]") {
  SECTION("v1 registry_version") {
    json scene_json = valid_scene_v2();
    scene_json.erase("scene_format_version");
    scene_json.erase("schema");
    scene_json["registry_version"] = 1;
    CHECK_FALSE(validate(scene_json));
  }

  SECTION("missing scene name") {
    json scene_json = valid_scene_v2();
    scene_json["scene"].erase("name");
    CHECK_FALSE(validate(scene_json));
  }

  SECTION("empty scene name") {
    json scene_json = valid_scene_v2();
    scene_json["scene"]["name"] = "";
    CHECK_FALSE(validate(scene_json));
  }

  SECTION("malformed entity guid") {
    json scene_json = valid_scene_v2();
    scene_json["entities"][0]["guid"] = "1";
    CHECK_FALSE(validate(scene_json));
  }

  SECTION("missing components object") {
    json scene_json = valid_scene_v2();
    scene_json["entities"][0].erase("components");
    CHECK_FALSE(validate(scene_json));
  }

  SECTION("empty entity name") {
    json scene_json = valid_scene_v2();
    scene_json["entities"][0]["name"] = "";
    CHECK_FALSE(validate(scene_json));
  }
}

TEST_CASE("JSON v2 scene validation rejects incompatible schema summaries",
          "[scene_serialization]") {
  SECTION("missing required component") {
    json scene_json = valid_scene_v2();
    scene_json["schema"]["required_components"].erase("teng.core.transform");
    CHECK_FALSE(validate(scene_json));
  }

  SECTION("unsupported component version") {
    json scene_json = valid_scene_v2();
    scene_json["schema"]["required_components"]["teng.core.transform"] = 2;
    CHECK_FALSE(validate(scene_json));
  }

  SECTION("missing required module") {
    json scene_json = valid_scene_v2();
    scene_json["schema"]["required_modules"] = json::array();
    CHECK_FALSE(validate(scene_json));
  }

  SECTION("unsupported module version") {
    json scene_json = valid_scene_v2();
    scene_json["schema"]["required_modules"][0]["version"] = 2;
    CHECK_FALSE(validate(scene_json));
  }
}

TEST_CASE("JSON v2 scene validation rejects invalid component payloads", "[scene_serialization]") {
  SECTION("unknown component") {
    json scene_json = valid_scene_v2();
    scene_json["entities"][0]["components"]["teng.core.unknown"] = json::object();
    CHECK_FALSE(validate(scene_json));
  }

  SECTION("runtime-only component") {
    json scene_json = valid_scene_v2();
    scene_json["entities"][0]["components"]["teng.core.local_to_world"] =
        json{{"value", json::array({1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                                    0.0, 0.0, 1.0})}};
    CHECK_FALSE(validate(scene_json));
  }

  SECTION("missing field") {
    json scene_json = valid_scene_v2();
    scene_json["entities"][0]["components"]["teng.core.transform"].erase("scale");
    CHECK_FALSE(validate(scene_json));
  }

  SECTION("wrong field type") {
    json scene_json = valid_scene_v2();
    scene_json["entities"][0]["components"]["teng.core.transform"]["translation"][0] = "x";
    CHECK_FALSE(validate(scene_json));
  }

  SECTION("bad AssetId") {
    json scene_json = valid_scene_v2();
    scene_json["entities"][0]["components"]["teng.core.mesh_renderable"]["model"] = "bad";
    CHECK_FALSE(validate(scene_json));
  }
}

TEST_CASE("JSON v2 scene save is deterministic and schema-valid", "[scene_serialization]") {
  const SceneTestContexts contexts = make_scene_test_contexts();
  SceneManager scenes(contexts.flecs_components);
  Scene& scene = scenes.create_scene("save determinism");
  scene.create_entity(EntityGuid{2}, "b");
  scene.create_entity(EntityGuid{0x10}, "a");

  Result<nlohmann::ordered_json> once_result =
      serialize_scene_to_json(scene, contexts.scene_serialization);
  Result<nlohmann::ordered_json> twice_result =
      serialize_scene_to_json(scene, contexts.scene_serialization);
  REQUIRE(once_result.has_value());
  REQUIRE(twice_result.has_value());
  const nlohmann::ordered_json& once = *once_result;
  const nlohmann::ordered_json& twice = *twice_result;
  CHECK(once.dump() == twice.dump());
  CHECK(validate_scene_file(contexts.scene_serialization, nlohmann::json::parse(once.dump()))
            .has_value());
  CHECK_FALSE(once.contains("registry_version"));
  CHECK(once.value("scene_format_version", 0) == 2);

  std::vector<std::string> top_level;
  for (const auto& [key, value] : once.items()) {
    (void)value;
    top_level.emplace_back(key);
  }
  CHECK(top_level ==
        std::vector<std::string>{"scene_format_version", "schema", "scene", "entities"});

  const auto& entities = once["entities"];
  REQUIRE(entities.size() == 2);
  CHECK(entities[0]["guid"].get<std::string>() == "0000000000000002");
  CHECK(entities[1]["guid"].get<std::string>() == "0000000000000010");

  const auto& transform = entities[0]["components"]["teng.core.transform"];
  const std::string transform_blob = transform.dump();
  const auto pos_translation = transform_blob.find("\"translation\"");
  const auto pos_rotation = transform_blob.find("\"rotation\"");
  const auto pos_scale = transform_blob.find("\"scale\"");
  CHECK(pos_translation < pos_rotation);
  CHECK(pos_rotation < pos_scale);
}

TEST_CASE("JSON v2 scene validation allows non-canonical extra data", "[scene_serialization]") {
  json scene_json = valid_scene_v2();
  scene_json["extra"] = true;
  scene_json["schema"]["components"] = json::object();
  scene_json["schema"]["required_modules"][0]["extra"] = true;
  scene_json["schema"]["required_components"]["teng.core.camera"] = 1;
  scene_json["scene"]["extra"] = true;
  scene_json["entities"][0]["parent"] = "0000000000000000";
  scene_json["entities"][0]["components"]["teng.core.transform"]["extra"] = 1;
  CHECK(validate(scene_json));
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::engine
