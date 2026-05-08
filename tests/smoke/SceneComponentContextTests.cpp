#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.hpp"
#include "core/Diagnostic.hpp"
#include "engine/scene/ComponentRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentContext.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneManager.hpp"
#include "fixtures_reflect.generated.hpp"

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
  const FlecsComponentContextBuilder builder{registry};
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
  FlecsComponentContext ctx = make_scene_component_context();  // NOLINT(misc-const-correctness)
  SceneManager scenes(ctx);                                    // NOLINT(misc-const-correctness)
  Scene& scene = scenes.create_scene("ctx_test");
  const flecs::entity entity = scene.create_entity();
  CHECK(entity.has<Transform>());
  CHECK(entity.has<LocalToWorld>());
  CHECK_FALSE(entity.has<Camera>());
}

TEST_CASE("generated fixture reflection records adapt into runtime builders",
          "[scene_component_context][reflection_codegen]") {
  scene::ComponentRegistryBuilder registry_builder;
  reflect_fixture_generated::register_fixture_reflected_components(registry_builder);

  scene::ComponentRegistry registry;
  DiagnosticReport registry_report;
  REQUIRE(registry_builder.try_freeze(registry, registry_report));
  CHECK_FALSE(registry_report.has_errors());
  REQUIRE(registry.components().size() == reflect_fixture_generated::k_component_count);

  const scene::FrozenComponentRecord* scalar = registry.find("teng.fixture.scalar_and_asset");
  REQUIRE(scalar != nullptr);
  CHECK(scalar->module_id == "teng.fixture");
  CHECK(scalar->storage == scene::ComponentStoragePolicy::Authored);
  REQUIRE(scalar->fields.size() == 5);
  CHECK(scalar->fields[0].key == "health");
  CHECK(scalar->fields[4].key == "attachment");
  REQUIRE(scalar->fields[4].asset.has_value());
  CHECK(scalar->fields[4].asset->expected_kind == "texture");

  const scene::FrozenComponentRecord* enum_policy = registry.find("teng.fixture.enum_and_policy");
  REQUIRE(enum_policy != nullptr);
  CHECK(enum_policy->storage == scene::ComponentStoragePolicy::RuntimeSession);
  CHECK(enum_policy->visibility == scene::ComponentSchemaVisibility::Hidden);
  REQUIRE(enum_policy->fields.size() == 2);
  REQUIRE(enum_policy->fields[0].enumeration.has_value());
  CHECK(enum_policy->fields[0].enumeration->enum_key == "teng.fixture.enum_and_policy_mode");

  FlecsComponentContextBuilder flecs_builder{registry};
  reflect_fixture_generated::register_fixture_reflected_flecs(registry, flecs_builder);
  FlecsComponentContext flecs_context;
  DiagnosticReport flecs_report;
  REQUIRE(flecs_builder.try_freeze(flecs_context, flecs_report));
  CHECK_FALSE(flecs_report.has_errors());
  CHECK(flecs_context.flecs_register_fns.size() == reflect_fixture_generated::k_component_count);

  SceneSerializationContextBuilder serialization_builder{registry};
  reflect_fixture_generated::register_fixture_reflected_serialization(serialization_builder);
  const SceneSerializationContext serialization = serialization_builder.freeze();
  REQUIRE(serialization.find_binding("teng.fixture.scalar_and_asset") != nullptr);
  CHECK(serialization.find_binding("teng.fixture.enum_and_policy") == nullptr);
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::engine
