#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "engine/scene/ComponentRegistry.hpp"

namespace teng::engine {

namespace {

[[nodiscard]] bool has_diagnostic(const core::DiagnosticReport& report, std::string_view code) {
  return std::ranges::any_of(report.diagnostics(), [code](const core::Diagnostic& diagnostic) {
    return diagnostic.code.value() == code;
  });
}

[[nodiscard]] ComponentDescriptor authored_component(std::string key) {
  return ComponentDescriptorBuilder{std::move(key)}
      .module("test.module")
      .storage(ComponentStoragePolicy::Authored)
      .field("value")
      .build();
}

}  // namespace

// NOLINTBEGIN(misc-use-anonymous-namespace): Catch2 TEST_CASE expands to static functions.

TEST_CASE("core component registry freezes with stable sorted components", "[component_registry]") {
  ComponentRegistryBuilder builder;
  register_core_components(builder);

  const ComponentRegistryFreezeResult result = builder.freeze();

  REQUIRE(!result.diagnostics.has_errors());
  REQUIRE(result.registry.has_value());

  const ComponentRegistry& registry = *result.registry;
  REQUIRE(registry.find_module("teng.core") != nullptr);
  REQUIRE(registry.find_component("teng.core.transform") != nullptr);
  REQUIRE(registry.find_component("teng.core.local_to_world") != nullptr);

  const ComponentDescriptor* transform = registry.find_component("teng.core.transform");
  REQUIRE(transform != nullptr);
  CHECK(transform->storage == ComponentStoragePolicy::Authored);
  REQUIRE(transform->fields.size() == 3);
  CHECK(transform->fields[0].key == "translation");
  CHECK(transform->fields[1].key == "rotation");
  CHECK(transform->fields[2].key == "scale");
  CHECK(registry.find_component_by_cooked_id(transform->cooked_id) == transform);

  std::vector<std::string> keys;
  for (const ComponentDescriptor& component : registry.components()) {
    keys.push_back(component.key);
  }
  CHECK(std::is_sorted(keys.begin(), keys.end()));
}

TEST_CASE("registry freeze reports duplicate component keys", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.module("test.module", 1)
      .component(authored_component("test.module.thing"))
      .component(authored_component("test.module.thing"));

  const ComponentRegistryFreezeResult result = builder.freeze();

  CHECK(result.diagnostics.has_errors());
  CHECK(!result.registry.has_value());
  CHECK(has_diagnostic(result.diagnostics, "schema.duplicate_component"));
}

TEST_CASE("registry freeze reports duplicate field keys", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.module("test.module", 1)
      .component(ComponentDescriptorBuilder{"test.module.thing"}
                     .module("test.module")
                     .field("value")
                     .field("value")
                     .build());

  const ComponentRegistryFreezeResult result = builder.freeze();

  CHECK(result.diagnostics.has_errors());
  CHECK(!result.registry.has_value());
  CHECK(has_diagnostic(result.diagnostics, "schema.duplicate_field"));
}

TEST_CASE("registry freeze reports invalid authored field policy", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.module("test.module", 1)
      .component(ComponentDescriptorBuilder{"test.module.empty"}
                     .module("test.module")
                     .storage(ComponentStoragePolicy::Authored)
                     .build());

  const ComponentRegistryFreezeResult result = builder.freeze();

  CHECK(result.diagnostics.has_errors());
  CHECK(!result.registry.has_value());
  CHECK(has_diagnostic(result.diagnostics, "schema.authored_component_without_fields"));
}

TEST_CASE("registry freeze allows runtime components without fields", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.module("test.module", 1)
      .component(ComponentDescriptorBuilder{"test.module.runtime"}
                     .module("test.module")
                     .storage(ComponentStoragePolicy::RuntimeSession)
                     .build());

  const ComponentRegistryFreezeResult result = builder.freeze();

  CHECK(!result.diagnostics.has_errors());
  REQUIRE(result.registry.has_value());
  REQUIRE(result.registry->find_component("test.module.runtime") != nullptr);
}

TEST_CASE("registry freeze reports cooked id collisions", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.module("test.module", 1)
      .component(ComponentDescriptorBuilder{"test.module.a"}
                     .module("test.module")
                     .field("value")
                     .cooked_id_for_testing(7)
                     .build())
      .component(ComponentDescriptorBuilder{"test.module.b"}
                     .module("test.module")
                     .field("value")
                     .cooked_id_for_testing(7)
                     .build());

  const ComponentRegistryFreezeResult result = builder.freeze();

  CHECK(result.diagnostics.has_errors());
  CHECK(!result.registry.has_value());
  CHECK(has_diagnostic(result.diagnostics, "schema.cooked_id_collision"));
}

TEST_CASE("registry freeze reports invalid keys and unknown modules", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.module("test.module", 1)
      .component(ComponentDescriptorBuilder{"BadKey"}
                     .module("missing.module")
                     .field("display name")
                     .build());

  const ComponentRegistryFreezeResult result = builder.freeze();

  CHECK(result.diagnostics.has_errors());
  CHECK(!result.registry.has_value());
  CHECK(has_diagnostic(result.diagnostics, "schema.invalid_component_key"));
  CHECK(has_diagnostic(result.diagnostics, "schema.unknown_module"));
  CHECK(has_diagnostic(result.diagnostics, "schema.invalid_field_key"));
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::engine
