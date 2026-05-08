#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "TestExtensionComponent.hpp"
#include "TestHelpers.hpp"
#include "core/Diagnostic.hpp"
#include "engine/scene/BuiltinComponentSerialization.hpp"
#include "engine/scene/ComponentSchemaJson.hpp"
#include "engine/scene/CoreComponentRegistrar.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneManager.hpp"
#include "engine/scene/SceneSerialization.hpp"

namespace teng::engine {
namespace {

using json = nlohmann::json;

[[nodiscard]] std::string sample_asset_id_text() {
  return AssetId::from_parts(0x123456789abcdef0ull, 0x00000000000000a1ull).to_string();
}

[[nodiscard]] json scene_json_with_extension_only_entity() {
  return json{
      {"scene_format_version", 2},
      {"schema",
       json{{"required_modules",
             json::array({json{{"id", std::string{k_test_extension_module_id}}, {"version", 1}}})},
            {"required_components",
             json{{std::string{k_test_extension_component_key}, 1}}}}},
      {"scene", json{{"name", "extension module test"}}},
      {"entities",
       json::array({json{
           {"guid", "0000000000000001"},
           {"components",
            json{{std::string{k_test_extension_component_key},
                  json{{"health", 42.5},
                       {"active", false},
                       {"kind", "beta"},
                       {"attachment", sample_asset_id_text()}}}}}}})}};
}

[[nodiscard]] bool report_contains_substring(const core::DiagnosticReport& report,
                                             std::string_view needle) {
  for (const core::Diagnostic& d : report.diagnostics()) {
    if (d.message.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

// NOLINTBEGIN(misc-use-anonymous-namespace)

TEST_CASE("test extension component JSON v2 round-trip", "[scene_serialization][extension]") {
  const SceneTestContexts contexts = make_scene_test_contexts_with_test_extension();
  SceneManager scenes(contexts.flecs_components);
  Scene& scene = scenes.create_scene("extension round-trip");
  const flecs::entity entity = scene.create_entity(EntityGuid{0x11}, "ext");
  const TestExtensionComponent written{
      .health = 3.25f,
      .active = false,
      .kind = "beta",
      .attachment = AssetId::from_parts(1, 2),
  };
  entity.set<TestExtensionComponent>(written);

  Result<nlohmann::ordered_json> json_result =
      serialize_scene_to_json(scene, contexts.scene_serialization);
  REQUIRE(json_result.has_value());
  const json round_json = json::parse(json_result->dump());
  const auto validated =
      validate_scene_file_full_report(contexts.scene_serialization, round_json);
  REQUIRE(validated.has_value());

  SceneManager loaded_scenes(contexts.flecs_components);
  Result<void> load_result =
      deserialize_scene_json(loaded_scenes, contexts.scene_serialization, round_json);
  REQUIRE(load_result.has_value());
  Scene* loaded = loaded_scenes.active_scene();
  REQUIRE(loaded != nullptr);
  const flecs::entity loaded_entity = loaded->find_entity(EntityGuid{0x11});
  REQUIRE(loaded_entity.is_valid());
  REQUIRE(loaded_entity.has<TestExtensionComponent>());
  const auto& read = loaded_entity.get<TestExtensionComponent>();
  CHECK(read.health == written.health);
  CHECK(read.active == written.active);
  CHECK(read.kind == written.kind);
  CHECK(read.attachment.high == written.attachment.high);
  CHECK(read.attachment.low == written.attachment.low);
}

TEST_CASE("serialize_component_schema_to_json includes test extension component",
          "[scene_serialization][extension]") {
  const SceneTestContexts contexts = make_scene_test_contexts_with_test_extension();
  Result<json> schema_json = serialize_component_schema_to_json(*contexts.component_registry);
  REQUIRE(schema_json.has_value());
  const json& components = schema_json->at("components");
  const std::string key{std::string{k_test_extension_component_key}};
  REQUIRE(components.contains(key));
  const json& comp = components[key];
  CHECK(comp["module_id"].get<std::string>() == k_test_extension_module_id);
  CHECK(comp["schema_version"].get<uint32_t>() == 1);
  const json& fields = comp["fields"];
  REQUIRE(fields.is_array());
  REQUIRE(fields.size() == 4);
  CHECK(fields[0]["key"].get<std::string>() == "health");
  CHECK(fields[1]["key"].get<std::string>() == "active");
  CHECK(fields[2]["key"].get<std::string>() == "kind");
  CHECK(fields[2]["enumeration"]["enum_key"].get<std::string>() ==
        "teng.test.extension_proof_kind");
  CHECK(fields[3]["key"].get<std::string>() == "attachment");
  CHECK(fields[3]["asset"]["expected_kind"].get<std::string>() == "texture");
}

TEST_CASE("core-only registry rejects scenes using test extension component",
          "[scene_serialization][extension]") {
  const SceneTestContexts core_only = make_scene_test_contexts();
  json scene_json = scene_json_with_extension_only_entity();
  const auto validated =
      validate_scene_file_full_report(core_only.scene_serialization, scene_json);
  REQUIRE_FALSE(validated.has_value());
  CHECK(report_contains_substring(validated.error(), "not registered"));
}

TEST_CASE("missing JSON binding for test extension fails validation", "[scene_serialization][extension]") {
  scene::ComponentRegistryBuilder component_registry_builder;
  register_core_components(component_registry_builder);
  register_test_extension_components(component_registry_builder);
  auto component_registry = std::make_unique<scene::ComponentRegistry>();
  core::DiagnosticReport freeze_report;
  REQUIRE(component_registry_builder.try_freeze(*component_registry, freeze_report));

  SceneSerializationContextBuilder serialization_builder{*component_registry};
  register_builtin_component_serialization(serialization_builder);
  const SceneSerializationContext serialization = serialization_builder.freeze();

  json scene_json = scene_json_with_extension_only_entity();
  const auto validated = validate_scene_file_full_report(serialization, scene_json);
  REQUIRE_FALSE(validated.has_value());
  CHECK(report_contains_substring(validated.error(), "JSON serialization binding"));
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::engine
