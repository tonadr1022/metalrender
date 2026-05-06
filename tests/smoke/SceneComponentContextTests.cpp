#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.hpp"
#include "core/Diagnostic.hpp"
#include "engine/scene/ComponentRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentContext.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneManager.hpp"

namespace teng::engine {

using core::DiagnosticReport;
using scene::ComponentRegistration;

namespace {

scene::ComponentRegistry make_component_registry(bool add_on_create = false) {
  scene::ComponentRegistryBuilder builder;
  builder.register_module("teng.test", 1);
  builder.register_component(ComponentRegistration{.component_key = "teng.test.x",
                                                   .module_id = "teng.test",
                                                   .module_version = 1,
                                                   .add_on_create = add_on_create});
  scene::ComponentRegistry registry;
  core::DiagnosticReport report;
  CHECK(builder.try_freeze(registry, report));
  CHECK(!report.has_errors());
  return registry;
}

void check_try_freeze_fails(const FlecsComponentContextBuilder& builder) {
  FlecsComponentContext out;
  DiagnosticReport report;
  CHECK_FALSE(builder.try_freeze(out, report));
  CHECK(report.has_errors());
}

}  // namespace

// NOLINTBEGIN(misc-use-anonymous-namespace)

TEST_CASE("SceneComponentContext freeze rejects component missing Flecs binding",
          "[scene_component_context]") {
  auto registry = make_component_registry();
  FlecsComponentContextBuilder builder{registry};
  check_try_freeze_fails(builder);
}

TEST_CASE("SceneComponentContext freeze rejects invalid Flecs bindings",
          "[scene_component_context]") {
  SECTION("null register_flecs_fn") {
    auto registry = make_component_registry(false);
    FlecsComponentContextBuilder builder{registry};
    builder.register_flecs_component(FlecsComponentBinding{.component_key = "teng.test.a",
                                                           .register_flecs_fn = nullptr,
                                                           .apply_on_create_fn = nullptr});
    check_try_freeze_fails(builder);
  }

  SECTION("add_on_create without apply_on_create_fn") {
    auto registry = make_component_registry(true);
    FlecsComponentContextBuilder builder{registry};
    builder.register_flecs_component(FlecsComponentBinding{
        .register_flecs_fn = [](flecs::world&) {}, .apply_on_create_fn = nullptr});
    check_try_freeze_fails(builder);
  }
}

TEST_CASE("core scene context creates entity with Transform and LocalToWorld without Camera",
          "[scene_component_context]") {
  FlecsComponentContext ctx = make_scene_component_context();
  SceneManager scenes(ctx);
  Scene& scene = scenes.create_scene("ctx_test");
  const flecs::entity entity = scene.create_entity();
  CHECK(entity.has<Transform>());
  CHECK(entity.has<LocalToWorld>());
  CHECK_FALSE(entity.has<Camera>());
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::engine
