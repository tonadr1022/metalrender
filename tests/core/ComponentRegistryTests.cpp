#include <catch2/catch_test_macros.hpp>

#include "core/ComponentRegistry.hpp"

namespace teng::core {

// NOLINTBEGIN(misc-use-anonymous-namespace): Catch2 TEST_CASE expands to static functions.

TEST_CASE("stable_component_id_v1 is deterministic", "[component_registry]") {
  CHECK(stable_component_id_v1("teng.core.transform") ==
        stable_component_id_v1("teng.core.transform"));
  CHECK(stable_component_id_v1("teng.core.transform") !=
        stable_component_id_v1("teng.core.camera"));
}

TEST_CASE("freeze succeeds for empty builder", "[component_registry]") {
  const ComponentRegistryBuilder builder;
  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(builder.try_freeze(registry, report));
  CHECK(!report.has_errors());
  CHECK(registry.components().empty());
}

TEST_CASE("freeze detects duplicate component key", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_component(
      ComponentRegistration{.component_key = "teng.core.a", .module_id = "teng.core"});
  builder.register_component(
      ComponentRegistration{.component_key = "teng.core.a", .module_id = "teng.core"});

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(!builder.try_freeze(registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.duplicate_component_key"});
}

TEST_CASE("freeze detects duplicate field key", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_component(ComponentRegistration{
      .component_key = "teng.core.a",
      .module_id = "teng.core",
      .field_keys = {"x", "y", "x"},
  });

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(!builder.try_freeze(registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.duplicate_field_key"});
}

TEST_CASE("freeze rejects default_on_create without Authored storage", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_component(
      ComponentRegistration{.component_key = "teng.core.a",
                            .module_id = "teng.core",
                            .storage = ComponentStoragePolicy::RuntimeDerived,
                            .default_on_create = true});

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(!builder.try_freeze(registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.invalid_storage_policy"});
}

TEST_CASE("freeze detects duplicate module registration", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_module("teng.core", 1);

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(!builder.try_freeze(registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.duplicate_module"});
}

TEST_CASE("freeze detects module version mismatch across registrations", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_module("teng.core", 2);

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(!builder.try_freeze(registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.module_version_mismatch"});
}

TEST_CASE("freeze detects unknown module on component", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_component(
      ComponentRegistration{.component_key = "game.foo.bar", .module_id = "game.missing"});

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(!builder.try_freeze(registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.unknown_module"});
}

TEST_CASE("freeze detects component module_version mismatch", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_component(ComponentRegistration{
      .component_key = "teng.core.a", .module_id = "teng.core", .module_version = 9});

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(!builder.try_freeze(registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code ==
        DiagnosticCode{"schema.component_module_version_mismatch"});
}

TEST_CASE("freeze succeeds for valid single component", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_component(ComponentRegistration{.component_key = "teng.core.transform",
                                                   .module_id = "teng.core",
                                                   .storage = ComponentStoragePolicy::Authored,
                                                   .default_on_create = true});

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(builder.try_freeze(registry, report));
  CHECK(!report.has_errors());
  REQUIRE(registry.components().size() == 1);
  CHECK(registry.components().front().component_key == "teng.core.transform");
  CHECK(registry.stable_component_id("teng.core.transform") ==
        stable_component_id_v1("teng.core.transform"));
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::core
