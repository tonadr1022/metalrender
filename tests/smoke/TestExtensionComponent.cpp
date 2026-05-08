#include "TestExtensionComponent.hpp"

#include <flecs.h>

#include <nlohmann/json.hpp>

namespace teng::engine {

namespace {

using json = nlohmann::json;
using scene::ComponentAssetFieldMetadata;
using scene::ComponentDefaultAssetId;
using scene::ComponentDefaultEnum;
using scene::ComponentEnumRegistration;
using scene::ComponentEnumValueRegistration;
using scene::ComponentFieldDefaultValue;
using scene::ComponentFieldKind;
using scene::ComponentSchemaVisibility;
using scene::ComponentStoragePolicy;

}  // namespace

void register_test_extension_components(scene::ComponentRegistryBuilder& builder) {
  builder.register_module(std::string{k_test_extension_module_id}, 1);
  builder.register_component({
      .component_key = std::string{k_test_extension_component_key},
      .module_id = std::string{k_test_extension_module_id},
      .module_version = 1,
      .schema_version = 1,
      .storage = ComponentStoragePolicy::Authored,
      .visibility = ComponentSchemaVisibility::Editable,
      .add_on_create = false,
      .fields =
          {
              {.key = "health",
               .kind = ComponentFieldKind::F32,
               .authored_required = true,
               .default_value = ComponentFieldDefaultValue{100.f}},
              {.key = "active",
               .kind = ComponentFieldKind::Bool,
               .authored_required = true,
               .default_value = ComponentFieldDefaultValue{true}},
              {.key = "kind",
               .kind = ComponentFieldKind::Enum,
               .authored_required = true,
               .default_value = ComponentFieldDefaultValue{ComponentDefaultEnum{.key = "alpha"}},
               .enumeration =
                   ComponentEnumRegistration{
                       .enum_key = "teng.test.extension_proof_kind",
                       .values =
                           {
                               ComponentEnumValueRegistration{.key = "alpha", .value = 0},
                               ComponentEnumValueRegistration{.key = "beta", .value = 1},
                           },
                   }},
              {.key = "attachment",
               .kind = ComponentFieldKind::AssetId,
               .authored_required = true,
               .default_value = ComponentFieldDefaultValue{ComponentDefaultAssetId{}},
               .asset = ComponentAssetFieldMetadata{.expected_kind = "texture"}},
          },
  });
}

void register_flecs_test_extension_components(FlecsComponentContextBuilder& builder) {
  builder.register_flecs_component(FlecsComponentBinding{
      .component_key = k_test_extension_component_key,
      .register_flecs_fn = [](flecs::world& world) { world.component<TestExtensionComponent>(); },
      .apply_on_create_fn = nullptr,
  });
}

void register_test_extension_serialization(SceneSerializationContextBuilder& builder) {
  builder.register_component({
      .component_key = k_test_extension_component_key,
      .has_component_fn = [](flecs::entity entity) { return entity.has<TestExtensionComponent>(); },
      .serialize_fn = [](flecs::entity entity) -> json {
        const auto& c = entity.get<TestExtensionComponent>();
        return json{{"health", c.health},
                    {"active", c.active},
                    {"kind", c.kind},
                    {"attachment", c.attachment.to_string()}};
      },
      .deserialize_fn = [](flecs::entity entity, const json& payload) -> void {
        entity.set<TestExtensionComponent>(
            {.health = payload["health"].get<float>(),
             .active = payload["active"].get<bool>(),
             .kind = payload["kind"].get<std::string>(),
             .attachment = AssetId::parse(payload["attachment"].get<std::string>()).value()});
      },
  });
}

}  // namespace teng::engine
