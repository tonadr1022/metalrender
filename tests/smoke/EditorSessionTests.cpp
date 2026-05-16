#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <system_error>

#include "TestHelpers.hpp"
#include "editor/EditorSession.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::editor {
namespace {

[[nodiscard]] engine::Scene& make_editor_test_scene(engine::SceneManager& scenes) {
  engine::Scene& scene = scenes.create_scene("editor test");
  scene.create_entity(engine::EntityGuid{0x10}, "subject");
  return scene;
}

[[nodiscard]] std::filesystem::path fresh_temp_dir(std::string_view name) {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / name;
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  return root;
}

}  // namespace

// NOLINTBEGIN(misc-use-anonymous-namespace): Catch2 TEST_CASE expands to static functions.

TEST_CASE("EditorSession binds an edit scene and creates a clean document", "[editor]") {
  const engine::SceneTestContexts contexts = engine::make_scene_test_contexts();
  engine::SceneManager scenes(contexts.flecs_components);
  const engine::Scene& scene = make_editor_test_scene(scenes);

  EditorSession session;
  const teng::Result<void> bound =
      session.bind(scenes, contexts.scene_serialization, scene.id(), std::nullopt);

  REQUIRE(bound);
  CHECK(session.bound());
  CHECK(session.mode() == EditorMode::Edit);
  CHECK(session.document_controller().edit_scene_id() == scene.id());
  CHECK(&session.document_controller().document().scene() == &scene);
  CHECK_FALSE(session.document_controller().dirty());
  CHECK(session.document_controller().revision() == 0);
  CHECK(session.document_controller().saved_revision() == 0);
}

TEST_CASE("EditorSession reports a missing edit scene without binding a document", "[editor]") {
  const engine::SceneTestContexts contexts = engine::make_scene_test_contexts();
  engine::SceneManager scenes(contexts.flecs_components);

  EditorSession session;
  const teng::Result<void> bound =
      session.bind(scenes, contexts.scene_serialization, engine::SceneId{0xfeed}, std::nullopt);

  CHECK_FALSE(bound);
  CHECK_FALSE(session.bound());
  CHECK_FALSE(session.last_status().empty());
  CHECK_FALSE(session.can_save());
}

TEST_CASE("EditorSession preserves document path and save availability", "[editor]") {
  const engine::SceneTestContexts contexts = engine::make_scene_test_contexts();
  engine::SceneManager scenes(contexts.flecs_components);
  const engine::Scene& scene = make_editor_test_scene(scenes);

  EditorSession unsaved_session;
  REQUIRE(unsaved_session.bind(scenes, contexts.scene_serialization, scene.id(), std::nullopt));
  CHECK_FALSE(unsaved_session.document_controller().path());
  CHECK_FALSE(unsaved_session.can_save());

  const std::filesystem::path root = fresh_temp_dir("metalrender_editor_session_path_tests");
  const std::filesystem::path path = root / "scene.tscene.json";

  EditorSession saved_session;
  REQUIRE(saved_session.bind(scenes, contexts.scene_serialization, scene.id(), path));
  CHECK(saved_session.document_controller().path() == std::optional<std::filesystem::path>{path});
  CHECK(saved_session.can_save());

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST_CASE("EditorSession save clears dirty state and advances saved revision", "[editor]") {
  const engine::SceneTestContexts contexts = engine::make_scene_test_contexts();
  engine::SceneManager scenes(contexts.flecs_components);
  const engine::Scene& scene = make_editor_test_scene(scenes);
  const std::filesystem::path root = fresh_temp_dir("metalrender_editor_session_save_tests");
  const std::filesystem::path path = root / "scene.tscene.json";

  EditorSession session;
  REQUIRE(session.bind(scenes, contexts.scene_serialization, scene.id(), path));

  teng::Result<engine::EntityGuid> created =
      session.document_controller().document().create_entity("created");
  REQUIRE(created);
  CHECK(session.document_controller().dirty());
  CHECK(session.document_controller().revision() == 1);
  CHECK(session.document_controller().saved_revision() == 0);

  const teng::Result<void> saved = session.save();
  REQUIRE(saved);
  CHECK_FALSE(session.document_controller().dirty());
  CHECK(session.document_controller().saved_revision() == session.document_controller().revision());
  CHECK(std::filesystem::exists(path));

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST_CASE("EditorSelection set and clear is independent from document save state", "[editor]") {
  const engine::SceneTestContexts contexts = engine::make_scene_test_contexts();
  engine::SceneManager scenes(contexts.flecs_components);
  const engine::Scene& scene = make_editor_test_scene(scenes);

  EditorSession session;
  REQUIRE(session.bind(scenes, contexts.scene_serialization, scene.id(), std::nullopt));
  CHECK_FALSE(session.document_controller().dirty());

  session.selection().select(engine::EntityGuid{0x10});
  CHECK(session.selection().selected() ==
        std::optional<engine::EntityGuid>{engine::EntityGuid{0x10}});
  CHECK_FALSE(session.document_controller().dirty());

  session.selection().clear();
  CHECK_FALSE(session.selection().selected());
  CHECK_FALSE(session.document_controller().dirty());
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::editor
