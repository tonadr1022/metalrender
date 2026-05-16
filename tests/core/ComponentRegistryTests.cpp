#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>

#include "core/Diagnostic.hpp"
#include "engine/scene/ComponentRegistry.hpp"

using teng::core::Diagnostic;
using teng::core::DiagnosticCode;
using teng::core::DiagnosticPath;
using teng::core::DiagnosticReport;
using namespace teng::engine::scene;

// NOLINTBEGIN(misc-use-anonymous-namespace): Catch2 TEST_CASE expands to static functions.

namespace {

void register_flecs_noop(flecs::world&) {}
void apply_on_create_noop(flecs::entity) {}
bool has_component_noop(flecs::entity) { return false; }
nlohmann::json serialize_component_noop(flecs::entity) { return nlohmann::json::object(); }
void deserialize_component_noop(flecs::entity, const nlohmann::json&) {}
void remove_component_noop(flecs::entity) {}

[[nodiscard]] ComponentTypeOps runtime_ops() {
  return ComponentTypeOps{.register_flecs_fn = register_flecs_noop};
}

[[nodiscard]] ComponentDescriptor component(
    std::string_view key, ComponentStoragePolicy storage = ComponentStoragePolicy::RuntimeSession,
    ComponentTypeOps ops = runtime_ops()) {
  return ComponentDescriptor{.component_key = key, .storage = storage, .ops = ops};
}

[[nodiscard]] ComponentModuleDescriptor module(std::string_view module_id,
                                               std::span<const ComponentDescriptor> components,
                                               uint32_t version = 1) {
  return ComponentModuleDescriptor{
      .module_id = module_id,
      .module_version = version,
      .components = components,
  };
}

[[nodiscard]] bool freeze(std::span<const ComponentModuleDescriptor> modules,
                          ComponentRegistry& registry, DiagnosticReport& report) {
  return try_freeze_component_registry(modules, registry, report);
}

void hook_adds_info(const FrozenComponentRecord& component, DiagnosticReport& report) {
  report.add_info(DiagnosticCode{"schema.validation_hook_test"}, DiagnosticPath{},
                  std::string{"hook saw "} + component.component_key);
}

void hook_adds_error(const FrozenComponentRecord&, DiagnosticReport& report) {
  report.add_error(DiagnosticCode{"schema.validation_hook_fail"}, DiagnosticPath{}, "hook failure");
}

}  // namespace

TEST_CASE("stable_component_id_v1 is deterministic", "[component_registry]") {
  CHECK(stable_component_id_v1("teng.core.transform") ==
        stable_component_id_v1("teng.core.transform"));
  CHECK(stable_component_id_v1("teng.core.transform") !=
        stable_component_id_v1("teng.core.camera"));
}

TEST_CASE("freeze succeeds for empty descriptors", "[component_registry]") {
  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(freeze(std::span<const ComponentModuleDescriptor>{}, registry, report));
  CHECK(!report.has_errors());
  CHECK(registry.components().empty());
  CHECK(registry.modules().empty());
}

TEST_CASE("freeze detects duplicate component key", "[component_registry]") {
  const std::array components{
      component("teng.core.a"),
      component("teng.core.a"),
  };
  const std::array modules{module("teng.core", components)};

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK_FALSE(freeze(modules, registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.duplicate_component_key"});
}

TEST_CASE("freeze detects duplicate module descriptors", "[component_registry]") {
  const std::array modules{
      module("teng.core", std::span<const ComponentDescriptor>{}),
      module("teng.core", std::span<const ComponentDescriptor>{}),
  };

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK_FALSE(freeze(modules, registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.duplicate_module"});
  CHECK(registry.modules().empty());
}

TEST_CASE("freeze detects module version mismatch across descriptors", "[component_registry]") {
  const std::array modules{
      module("teng.core", std::span<const ComponentDescriptor>{}, 1),
      module("teng.core", std::span<const ComponentDescriptor>{}, 2),
  };

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK_FALSE(freeze(modules, registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.module_version_mismatch"});
}

TEST_CASE("freeze succeeds for valid component descriptors", "[component_registry]") {
  const std::array components{
      ComponentDescriptor{
          .component_key = "teng.core.transform",
          .storage = ComponentStoragePolicy::Authored,
          .add_on_create = true,
          .ops =
              ComponentTypeOps{
                  .register_flecs_fn = register_flecs_noop,
                  .apply_on_create_fn = apply_on_create_noop,
                  .has_component_fn = has_component_noop,
                  .serialize_fn = serialize_component_noop,
                  .deserialize_fn = deserialize_component_noop,
                  .remove_fn = remove_component_noop,
              },
      },
  };
  const std::array modules{module("teng.core", components)};

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(freeze(modules, registry, report));
  CHECK(!report.has_errors());
  REQUIRE(registry.components().size() == 1);
  CHECK(registry.components().front().component_key == "teng.core.transform");
  CHECK(registry.components().front().module_id == "teng.core");
  CHECK(registry.components().front().add_on_create);
  CHECK(registry.components().front().ops.apply_on_create_fn == apply_on_create_noop);
  CHECK(registry.components().front().ops.remove_fn == remove_component_noop);
  CHECK(registry.stable_component_id("teng.core.transform") ==
        stable_component_id_v1("teng.core.transform"));
  REQUIRE(registry.modules().size() == 1);
  CHECK(registry.modules().front() == FrozenModuleRecord{"teng.core", 1});
}

TEST_CASE("frozen modules are sorted by module id", "[component_registry]") {
  const std::array core_components{component("teng.core.a")};
  const std::array game_components{component("teng.game.b")};
  const std::array audio_components{component("teng.audio.c")};
  const std::array modules{
      module("teng.game", game_components, 3),
      module("teng.core", core_components, 1),
      module("teng.audio", audio_components, 2),
  };

  ComponentRegistry registry;
  DiagnosticReport report;
  REQUIRE(freeze(modules, registry, report));
  REQUIRE(registry.modules().size() == 3);
  CHECK(registry.modules()[0] == FrozenModuleRecord{"teng.audio", 2});
  CHECK(registry.modules()[1] == FrozenModuleRecord{"teng.core", 1});
  CHECK(registry.modules()[2] == FrozenModuleRecord{"teng.game", 3});
}

TEST_CASE("freeze preserves field facts", "[component_registry]") {
  const std::array fields{
      ComponentFieldDescriptor{
          .key = "flag",
          .member_name = "is_enabled",
          .kind = ComponentFieldKind::Bool,
          .default_value = ComponentFieldDefaultValue{true},
          .script_exposure = ScriptExposure::ReadWrite,
      },
      ComponentFieldDescriptor{
          .key = "tex",
          .member_name = "texture",
          .kind = ComponentFieldKind::AssetId,
          .default_value = ComponentFieldDefaultValue{ComponentDefaultAssetId{.value = "id42"}},
          .asset = ComponentAssetFieldMetadata{.expected_kind = "texture"},
      },
      ComponentFieldDescriptor{
          .key = "tier",
          .member_name = "tier",
          .kind = ComponentFieldKind::Enum,
          .default_value = ComponentFieldDefaultValue{ComponentDefaultEnum{.value = 10}},
          .enumeration =
              ComponentEnumRegistration{
                  .enum_key = "tier_kind",
                  .values = {{.key = "free", .value = 0}, {.key = "pro", .value = 10}},
              },
      },
  };
  const std::array components{ComponentDescriptor{
      .component_key = "teng.core.defaults",
      .storage = ComponentStoragePolicy::RuntimeSession,
      .fields = fields,
      .ops = runtime_ops(),
  }};
  const std::array modules{module("teng.core", components)};

  ComponentRegistry registry;
  DiagnosticReport report;
  REQUIRE(freeze(modules, registry, report));
  const FrozenComponentRecord* rec = registry.find("teng.core.defaults");
  REQUIRE(rec != nullptr);
  REQUIRE(rec->fields.size() == 3);
  CHECK(rec->fields[0].key == "flag");
  CHECK(rec->fields[0].member_name == "is_enabled");
  CHECK(std::get<bool>(rec->fields[0].default_value));
  CHECK(rec->fields[0].script_exposure == ScriptExposure::ReadWrite);
  CHECK(std::get<ComponentDefaultAssetId>(rec->fields[1].default_value).value == "id42");
  if (const auto& asset_meta = rec->fields[1].asset; asset_meta) {
    CHECK(asset_meta->expected_kind == "texture");
  } else {
    FAIL("expected asset field metadata");
  }
  if (const auto& tier_enumeration = rec->fields[2].enumeration; tier_enumeration) {
    CHECK(tier_enumeration->enum_key == "tier_kind");
  } else {
    FAIL("expected tier enumeration metadata");
  }
  CHECK(std::get<ComponentDefaultEnum>(rec->fields[2].default_value).value == 10);
}

TEST_CASE("freeze validates component operation policy", "[component_registry]") {
  SECTION("missing Flecs ops on non-EditorOnly component") {
    const std::array components{
        component("teng.core.a", ComponentStoragePolicy::RuntimeSession, ComponentTypeOps{}),
    };
    const std::array modules{module("teng.core", components)};
    ComponentRegistry registry;
    DiagnosticReport report;
    CHECK_FALSE(freeze(modules, registry, report));
    REQUIRE(report.has_errors());
    CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.missing_register_flecs_fn"});
  }

  SECTION("missing apply-on-create op") {
    const std::array components{ComponentDescriptor{
        .component_key = "teng.core.a",
        .storage = ComponentStoragePolicy::RuntimeSession,
        .add_on_create = true,
        .ops = runtime_ops(),
    }};
    const std::array modules{module("teng.core", components)};
    ComponentRegistry registry;
    DiagnosticReport report;
    CHECK_FALSE(freeze(modules, registry, report));
    REQUIRE(report.has_errors());
    CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.missing_apply_on_create_fn"});
  }

  SECTION("missing JSON ops on Authored component") {
    const std::array components{
        component("teng.core.a", ComponentStoragePolicy::Authored, runtime_ops()),
    };
    const std::array modules{module("teng.core", components)};
    ComponentRegistry registry;
    DiagnosticReport report;
    CHECK_FALSE(freeze(modules, registry, report));
    REQUIRE(report.has_errors());
    CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.missing_has_component_fn"});
  }

  SECTION("EditorOnly components do not require ops") {
    const std::array components{
        component("teng.core.editor", ComponentStoragePolicy::EditorOnly, ComponentTypeOps{}),
    };
    const std::array modules{module("teng.core", components)};
    ComponentRegistry registry;
    DiagnosticReport report;
    CHECK(freeze(modules, registry, report));
    CHECK(!report.has_errors());
  }
}

TEST_CASE("freeze invokes schema validation hook", "[component_registry]") {
  const std::array components{ComponentDescriptor{
      .component_key = "teng.core.a",
      .storage = ComponentStoragePolicy::RuntimeSession,
      .schema_validation_hook = hook_adds_info,
      .ops = runtime_ops(),
  }};
  const std::array modules{module("teng.core", components)};

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(freeze(modules, registry, report));
  CHECK(!report.has_errors());
  CHECK(std::ranges::any_of(report.diagnostics(), [](const Diagnostic& d) {
    return d.code == DiagnosticCode{"schema.validation_hook_test"} &&
           d.message.contains("teng.core.a");
  }));
}

TEST_CASE("freeze fails when schema validation hook reports error", "[component_registry]") {
  const std::array components{ComponentDescriptor{
      .component_key = "teng.core.a",
      .storage = ComponentStoragePolicy::RuntimeSession,
      .schema_validation_hook = hook_adds_error,
      .ops = runtime_ops(),
  }};
  const std::array modules{module("teng.core", components)};

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK_FALSE(freeze(modules, registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.validation_hook_fail"});
}

// NOLINTEND(misc-use-anonymous-namespace)
