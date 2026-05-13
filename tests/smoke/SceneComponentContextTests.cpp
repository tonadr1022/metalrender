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

// NOLINTBEGIN(misc-use-anonymous-namespace)

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

TEST_CASE("generated fixture descriptors adapt into runtime contexts",
          "[scene_component_context][reflection_codegen]") {
  scene::ComponentRegistry registry;
  DiagnosticReport registry_report;
  REQUIRE(scene::try_freeze_component_registry(reflect_fixture_generated::fixture_modules(),
                                               registry, registry_report));
  CHECK_FALSE(registry_report.has_errors());
  REQUIRE(registry.components().size() == reflect_fixture_generated::k_component_count);

  const scene::FrozenComponentRecord* scalar = registry.find("teng.fixture.scalar_and_asset");
  REQUIRE(scalar != nullptr);
  CHECK(scalar->module_id == "teng.fixture");
  CHECK(scalar->storage == scene::ComponentStoragePolicy::Authored);
  REQUIRE(scalar->fields.size() == 5);
  CHECK(scalar->fields[0].key == "health");
  CHECK(scalar->fields[4].key == "attachment");
  if (const auto& attachment_asset = scalar->fields[4].asset; attachment_asset) {
    CHECK(attachment_asset->expected_kind == "texture");
  } else {
    FAIL("expected attachment field to declare asset metadata");
  }

  const scene::FrozenComponentRecord* enum_policy = registry.find("teng.fixture.enum_and_policy");
  REQUIRE(enum_policy != nullptr);
  CHECK(enum_policy->storage == scene::ComponentStoragePolicy::RuntimeSession);
  CHECK(enum_policy->visibility == scene::ComponentSchemaVisibility::Hidden);
  REQUIRE(enum_policy->fields.size() == 2);
  if (const auto& mode_enumeration = enum_policy->fields[0].enumeration; mode_enumeration) {
    CHECK(mode_enumeration->enum_key == "teng.fixture.enum_and_policy_mode");
  } else {
    FAIL("expected mode field to declare enumeration metadata");
  }

  FlecsComponentContext flecs_context;
  DiagnosticReport flecs_report;
  REQUIRE(make_flecs_component_context(registry, flecs_context, flecs_report));
  CHECK_FALSE(flecs_report.has_errors());
  CHECK(flecs_context.flecs_register_fns.size() == reflect_fixture_generated::k_component_count);

  const SceneSerializationContext serialization = make_scene_serialization_context(registry);
  REQUIRE(serialization.find_authored_component("teng.fixture.scalar_and_asset") != nullptr);
  CHECK(serialization.find_authored_component("teng.fixture.enum_and_policy") == nullptr);
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::engine
