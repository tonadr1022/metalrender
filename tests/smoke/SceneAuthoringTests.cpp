#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <system_error>

#include "TestHelpers.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneManager.hpp"
#include "engine/scene/SceneSerialization.hpp"
#include "engine/scene/authoring/SceneAuthoringInspector.hpp"
#include "engine/scene/authoring/SceneDocument.hpp"

namespace teng::engine {
namespace {

using nlohmann::json;
using scene::authoring::SceneDocument;

[[nodiscard]] Scene& make_test_scene(SceneManager& scenes) {
  Scene& scene = scenes.create_scene("authoring test");
  scene.create_entity(EntityGuid{0x10}, "subject");
  return scene;
}

[[nodiscard]] nlohmann::ordered_json serialized(Scene& scene,
                                                const SceneSerializationContext& serialization) {
  Result<nlohmann::ordered_json> out = serialize_scene_to_json(scene, serialization);
  REQUIRE(out);
  return *out;
}

}  // namespace

// NOLINTBEGIN(misc-use-anonymous-namespace): Catch2 TEST_CASE expands to static functions.

TEST_CASE("SceneDocument tracks dirty state and save revisions", "[scene_authoring]") {
  const SceneTestContexts contexts = make_scene_test_contexts();
  SceneManager scenes(contexts.flecs_components);
  Scene& scene = make_test_scene(scenes);
  SceneDocument document(scene, contexts.scene_serialization);

  CHECK_FALSE(document.dirty());
  CHECK(document.revision() == 0);
  CHECK(document.saved_revision() == 0);

  Result<EntityGuid> created = document.create_entity("created");
  REQUIRE(created);
  CHECK(document.dirty());
  CHECK(document.revision() == 1);
  CHECK(scene.has_entity(*created));

  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "metalrender_scene_authoring_tests";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  const std::filesystem::path path = root / "saved.tscene.json";

  Result<void> saved = document.save_as(path);
  CHECK(saved);
  CHECK_FALSE(document.dirty());
  CHECK(document.saved_revision() == document.revision());
  CHECK(document.path() == std::optional<std::filesystem::path>{path});
  CHECK(std::filesystem::exists(path));

  std::filesystem::remove_all(root, ec);
}

TEST_CASE("SceneDocument rejects invalid field edits without mutation", "[scene_authoring]") {
  const SceneTestContexts contexts = make_scene_test_contexts();
  SceneManager scenes(contexts.flecs_components);
  Scene& scene = make_test_scene(scenes);
  SceneDocument document(scene, contexts.scene_serialization);

  const nlohmann::ordered_json before = serialized(scene, contexts.scene_serialization);
  Result<void> edited = document.edit_component_field(EntityGuid{0x10}, "teng.core.transform",
                                                      "translation", json{"not a vec3"});
  CHECK_FALSE(edited);
  CHECK(document.revision() == 0);
  CHECK_FALSE(document.dirty());
  CHECK(serialized(scene, contexts.scene_serialization) == before);
}

TEST_CASE("SceneDocument component commands validate registration and authored storage",
          "[scene_authoring]") {
  const SceneTestContexts contexts = make_scene_test_contexts();
  SceneManager scenes(contexts.flecs_components);
  Scene& scene = make_test_scene(scenes);
  SceneDocument document(scene, contexts.scene_serialization);

  CHECK_FALSE(document.add_component(EntityGuid{0x10}, "teng.missing.component"));
  CHECK_FALSE(document.add_component(EntityGuid{0x10}, "teng.core.local_to_world"));
  CHECK(document.revision() == 0);

  Result<void> added = document.add_component(EntityGuid{0x10}, "teng.core.camera");
  REQUIRE(added);
  const flecs::entity entity = scene.find_entity(EntityGuid{0x10});
  REQUIRE(entity.is_valid());
  REQUIRE(entity.has<Camera>());
  CHECK_FALSE(entity.get<Camera>().primary);

  Result<void> set = document.set_component(
      EntityGuid{0x10}, "teng.core.camera",
      json{{"fov_y", 0.75f}, {"z_near", 0.25f}, {"z_far", 500.f}, {"primary", true}});
  REQUIRE(set);
  CHECK(entity.get<Camera>().primary);
  CHECK(document.revision() == 2);

  Result<void> removed = document.remove_component(EntityGuid{0x10}, "teng.core.camera");
  REQUIRE(removed);
  CHECK_FALSE(entity.has<Camera>());
  CHECK(document.revision() == 3);
}

TEST_CASE("Scene authoring inspector exposes editable fields for field commits",
          "[scene_authoring]") {
  const SceneTestContexts contexts = make_scene_test_contexts();
  SceneManager scenes(contexts.flecs_components);
  Scene& scene = make_test_scene(scenes);
  SceneDocument document(scene, contexts.scene_serialization);

  const std::vector<scene::authoring::InspectorComponentInfo> inspector =
      scene::authoring::editable_component_inspector(contexts.scene_serialization);
  const auto transform = std::ranges::find(
      inspector, "teng.core.transform", &scene::authoring::InspectorComponentInfo::component_key);
  REQUIRE(transform != inspector.end());
  CHECK(std::ranges::any_of(transform->fields, [](const auto& field) {
    return field.key == "translation" && field.kind == scene::ComponentFieldKind::Vec3;
  }));

  Result<void> edited = document.edit_component_field(EntityGuid{0x10}, "teng.core.transform",
                                                      "translation", json::array({2.f, 3.f, 4.f}));
  REQUIRE(edited);
  const auto* transform_component = scene.find_entity(EntityGuid{0x10}).try_get<Transform>();
  REQUIRE(transform_component);
  CHECK(transform_component->translation.x == 2.f);
  CHECK(transform_component->translation.y == 3.f);
  CHECK(transform_component->translation.z == 4.f);
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::engine
