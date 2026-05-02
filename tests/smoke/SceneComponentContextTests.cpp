#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.hpp"
#include "core/ComponentRegistry.hpp"
#include "core/Diagnostic.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentContext.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::engine {

using core::ComponentRegistration;
using core::DiagnosticReport;

namespace {

void check_try_freeze_fails(const SceneComponentContextBuilder& builder) {
  SceneComponentContext out;
  DiagnosticReport report;
  CHECK_FALSE(builder.try_freeze(out, report));
  CHECK(report.has_errors());
  CHECK(out.registry.components().empty());
}

}  // namespace

// NOLINTBEGIN(misc-use-anonymous-namespace)

TEST_CASE("SceneComponentContext freeze rejects component missing Flecs binding",
          "[scene_component_context]") {
  SceneComponentContextBuilder builder;
  builder.registry_builder().register_module("teng.test", 1);
  builder.registry_builder().register_component(ComponentRegistration{
      .component_key = "teng.test.x", .module_id = "teng.test", .module_version = 1});
  check_try_freeze_fails(builder);
}

TEST_CASE("SceneComponentContext freeze rejects invalid Flecs bindings",
          "[scene_component_context]") {
  SECTION("null register_flecs_fn") {
    SceneComponentContextBuilder builder;
    builder.registry_builder().register_module("teng.test", 1);
    builder.register_component(
        ComponentRegistration{
            .component_key = "teng.test.a", .module_id = "teng.test", .module_version = 1},
        FlecsComponentBinding{.register_flecs_fn = nullptr, .apply_on_create_fn = nullptr});
    check_try_freeze_fails(builder);
  }

  SECTION("add_on_create without apply_on_create_fn") {
    SceneComponentContextBuilder builder;
    builder.registry_builder().register_module("teng.test", 1);
    builder.register_component(ComponentRegistration{.component_key = "teng.test.b",
                                                     .module_id = "teng.test",
                                                     .module_version = 1,
                                                     .add_on_create = true},
                               FlecsComponentBinding{.register_flecs_fn = [](flecs::world&) {},
                                                     .apply_on_create_fn = nullptr});
    check_try_freeze_fails(builder);
  }
}

TEST_CASE("core scene context creates entity with Transform and LocalToWorld without Camera",
          "[scene_component_context]") {
  SceneComponentContext ctx = make_scene_component_context();
  SceneManager scenes(ctx);
  Scene& scene = scenes.create_scene("ctx_test");
  const flecs::entity entity = scene.create_entity();
  CHECK(entity.has<Transform>());
  CHECK(entity.has<LocalToWorld>());
  CHECK_FALSE(entity.has<Camera>());
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::engine
