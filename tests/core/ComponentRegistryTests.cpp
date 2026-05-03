#include <catch2/catch_test_macros.hpp>
#include <string>

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
  CHECK(registry.modules().empty());
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

TEST_CASE("freeze detects duplicate module registration", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_module("teng.core", 1);

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(!builder.try_freeze(registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.duplicate_module"});
  CHECK(registry.modules().empty());
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
  CHECK(registry.modules().empty());
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
                                                   .add_on_create = true});

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(builder.try_freeze(registry, report));
  CHECK(!report.has_errors());
  REQUIRE(registry.components().size() == 1);
  CHECK(registry.components().front().component_key == "teng.core.transform");
  CHECK(registry.stable_component_id("teng.core.transform") ==
        stable_component_id_v1("teng.core.transform"));
  REQUIRE(registry.modules().size() == 1);
  CHECK(registry.modules().front().module_id == "teng.core");
  CHECK(registry.modules().front().version == 1);
  const FrozenModuleRecord* mod = registry.find_module("teng.core");
  REQUIRE(mod != nullptr);
  CHECK(mod->version == 1);
  CHECK(registry.find_module("teng.missing") == nullptr);
}

TEST_CASE("frozen modules are sorted by module id", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.game", 3);
  builder.register_module("teng.core", 1);
  builder.register_module("teng.audio", 2);
  builder.register_component(
      ComponentRegistration{.component_key = "teng.core.a", .module_id = "teng.core"});
  builder.register_component(ComponentRegistration{
      .component_key = "teng.game.b", .module_id = "teng.game", .module_version = 3});
  builder.register_component(ComponentRegistration{
      .component_key = "teng.audio.c", .module_id = "teng.audio", .module_version = 2});

  ComponentRegistry registry;
  DiagnosticReport report;
  REQUIRE(builder.try_freeze(registry, report));
  REQUIRE(registry.modules().size() == 3);
  CHECK(registry.modules()[0] == FrozenModuleRecord{"teng.audio", 2});
  CHECK(registry.modules()[1] == FrozenModuleRecord{"teng.core", 1});
  CHECK(registry.modules()[2] == FrozenModuleRecord{"teng.game", 3});
  REQUIRE(registry.find_module("teng.game") != nullptr);
  CHECK(registry.find_module("teng.game")->version == 3);
  CHECK(registry.find_module("teng.core")->version == 1);
  CHECK(registry.find_module("teng.audio")->version == 2);
}

TEST_CASE("freeze retains frozen modules when no components", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_module("teng.game", 2);

  ComponentRegistry registry;
  DiagnosticReport report;
  REQUIRE(builder.try_freeze(registry, report));
  CHECK(registry.components().empty());
  REQUIRE(registry.modules().size() == 2);
  CHECK(registry.modules()[0].module_id == "teng.core");
  CHECK(registry.modules()[1].module_id == "teng.game");
}

TEST_CASE("freeze preserves field order", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_component(ComponentRegistration{
      .component_key = "teng.core.ordered",
      .module_id = "teng.core",
      .fields =
          {
              {.key = "first", .kind = ComponentFieldKind::Bool, .authored_required = true},
              {.key = "second", .kind = ComponentFieldKind::F32, .authored_required = true},
              {.key = "third", .kind = ComponentFieldKind::Vec3, .authored_required = true},
          },
  });

  ComponentRegistry registry;
  DiagnosticReport report;
  REQUIRE(builder.try_freeze(registry, report));
  const FrozenComponentRecord* rec = registry.find("teng.core.ordered");
  REQUIRE(rec != nullptr);
  REQUIRE(rec->fields.size() == 3);
  CHECK(rec->fields[0].key == "first");
  CHECK(rec->fields[1].key == "second");
  CHECK(rec->fields[2].key == "third");
}

TEST_CASE("freeze preserves default values", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_component(ComponentRegistration{
      .component_key = "teng.core.defaults",
      .module_id = "teng.core",
      .fields =
          {
              {.key = "flag",
               .kind = ComponentFieldKind::Bool,
               .authored_required = true,
               .default_value = ComponentFieldDefaultValue{true}},
              {.key = "heat",
               .kind = ComponentFieldKind::F32,
               .authored_required = true,
               .default_value = ComponentFieldDefaultValue{2.5f}},
              {.key = "pos",
               .kind = ComponentFieldKind::Vec3,
               .authored_required = true,
               .default_value = ComponentFieldDefaultValue{ComponentDefaultVec3{1.f, 2.f, 3.f}}},
              {.key = "orient",
               .kind = ComponentFieldKind::Quat,
               .authored_required = true,
               .default_value =
                   ComponentFieldDefaultValue{ComponentDefaultQuat{0.7f, 0.f, 0.7f, 0.f}}},
              {.key = "tex",
               .kind = ComponentFieldKind::AssetId,
               .authored_required = true,
               .default_value =
                   ComponentFieldDefaultValue{ComponentDefaultAssetId{.value = "id42"}}},
              {
                  .key = "tier",
                  .kind = ComponentFieldKind::Enum,
                  .authored_required = true,
                  .default_value = ComponentFieldDefaultValue{ComponentDefaultEnum{.key = "pro"}},
                  .enumeration =
                      ComponentEnumRegistration{
                          .enum_key = "tier_kind",
                          .values = {{.key = "free", .value = 0}, {.key = "pro", .value = 10}},
                      },
              },
          },
  });

  ComponentRegistry registry;
  DiagnosticReport report;
  REQUIRE(builder.try_freeze(registry, report));
  const FrozenComponentRecord* rec = registry.find("teng.core.defaults");
  REQUIRE(rec != nullptr);
  REQUIRE(rec->fields.size() == 6);

  REQUIRE(rec->fields[0].default_value.has_value());
  CHECK(std::get<bool>(*rec->fields[0].default_value) == true);

  REQUIRE(rec->fields[1].default_value.has_value());
  CHECK(std::get<float>(*rec->fields[1].default_value) == 2.5f);

  REQUIRE(rec->fields[2].default_value.has_value());
  {
    const auto& v = std::get<ComponentDefaultVec3>(*rec->fields[2].default_value);
    CHECK(v.x == 1.f);
    CHECK(v.y == 2.f);
    CHECK(v.z == 3.f);
  }

  REQUIRE(rec->fields[3].default_value.has_value());
  {
    const auto& q = std::get<ComponentDefaultQuat>(*rec->fields[3].default_value);
    CHECK(q.w == 0.7f);
    CHECK(q.x == 0.f);
    CHECK(q.y == 0.7f);
    CHECK(q.z == 0.f);
  }

  REQUIRE(rec->fields[4].default_value.has_value());
  CHECK(std::get<ComponentDefaultAssetId>(*rec->fields[4].default_value).value == "id42");

  REQUIRE(rec->fields[5].default_value.has_value());
  CHECK(std::get<ComponentDefaultEnum>(*rec->fields[5].default_value).key == "pro");
}

namespace {

void hook_adds_info(const FrozenComponentRecord& component, DiagnosticReport& report) {
  report.add_info(DiagnosticCode{"schema.validation_hook_test"}, DiagnosticPath{},
                  std::string{"hook saw "} + component.component_key);
}

void hook_adds_error(const FrozenComponentRecord&, DiagnosticReport& report) {
  report.add_error(DiagnosticCode{"schema.validation_hook_fail"}, DiagnosticPath{}, "hook failure");
}

}  // namespace

TEST_CASE("freeze invokes schema validation hook", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_component(ComponentRegistration{.component_key = "teng.core.a",
                                                   .module_id = "teng.core",
                                                   .schema_validation_hook = hook_adds_info});

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(builder.try_freeze(registry, report));
  CHECK(!report.has_errors());
  bool saw_hook = false;
  for (const Diagnostic& d : report.diagnostics()) {
    if (d.code == DiagnosticCode{"schema.validation_hook_test"}) {
      saw_hook = true;
      CHECK(d.message.contains("teng.core.a"));
    }
  }
  CHECK(saw_hook);
}

TEST_CASE("freeze fails when schema validation hook reports error", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_component(ComponentRegistration{.component_key = "teng.core.a",
                                                   .module_id = "teng.core",
                                                   .schema_validation_hook = hook_adds_error});

  ComponentRegistry registry;
  DiagnosticReport report;
  CHECK(!builder.try_freeze(registry, report));
  REQUIRE(report.has_errors());
  CHECK(report.diagnostics().front().code == DiagnosticCode{"schema.validation_hook_fail"});
}

TEST_CASE("freeze preserves schema visibility", "[component_registry]") {
  ComponentRegistryBuilder builder;
  builder.register_module("teng.core", 1);
  builder.register_component(ComponentRegistration{
      .component_key = "teng.core.hidden",
      .module_id = "teng.core",
      .visibility = ComponentSchemaVisibility::Hidden,
  });
  builder.register_component(ComponentRegistration{
      .component_key = "teng.core.inspect",
      .module_id = "teng.core",
      .visibility = ComponentSchemaVisibility::DebugInspectable,
  });

  ComponentRegistry registry;
  DiagnosticReport report;
  REQUIRE(builder.try_freeze(registry, report));
  CHECK(registry.find("teng.core.hidden")->visibility == ComponentSchemaVisibility::Hidden);
  CHECK(registry.find("teng.core.inspect")->visibility ==
        ComponentSchemaVisibility::DebugInspectable);
}

// NOLINTEND(misc-use-anonymous-namespace)

}  // namespace teng::core
