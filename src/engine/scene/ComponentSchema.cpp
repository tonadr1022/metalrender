#include <nlohmann/json.hpp>

#include "ComponentSchemaJson.hpp"
#include "engine/scene/ComponentRegistry.hpp"

namespace teng::engine {

using json = nlohmann::json;

namespace {

[[nodiscard]] json component_field_default_value_to_json(
    const scene::ComponentFieldDefaultValue& value) {
  return std::visit(
      [](const auto& v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int64_t> ||
                      std::is_same_v<T, uint64_t> || std::is_same_v<T, float> ||
                      std::is_same_v<T, std::string>) {
          return v;
        } else if constexpr (std::is_same_v<T, scene::ComponentDefaultVec2>) {
          return json::array({v.x, v.y});
        } else if constexpr (std::is_same_v<T, scene::ComponentDefaultVec3>) {
          return json::array({v.x, v.y, v.z});
        } else if constexpr (std::is_same_v<T, scene::ComponentDefaultVec4>) {
          return json::array({v.x, v.y, v.z, v.w});
        } else if constexpr (std::is_same_v<T, scene::ComponentDefaultQuat>) {
          return json::array({v.w, v.x, v.y, v.z});
        } else if constexpr (std::is_same_v<T, scene::ComponentDefaultMat4>) {
          return json(v.elements);
        } else if constexpr (std::is_same_v<T, scene::ComponentDefaultEnum> ||
                             std::is_same_v<T, scene::ComponentDefaultAssetId>) {
          return json(v.value);
        } else {
          static_assert(sizeof(T) == 0, "unhandled ComponentFieldDefaultValue alternative");
        }
      },
      value);
}

json asset_field_metadata_to_json(const scene::ComponentAssetFieldMetadata& metadata) {
  return json{{"expected_kind", metadata.expected_kind}};
}

}  // namespace

Result<nlohmann::json> serialize_component_schema_to_json(
    const scene::ComponentRegistry& registry) {
  json schema = json::object();
  schema["components"] = json::object();
  auto& components = schema["components"];
  for (const auto& component : registry.components()) {
    json component_json = json::object();
    component_json["module_id"] = component.module_id;
    component_json["module_version"] = component.module_version;
    component_json["schema_version"] = component.schema_version;
    component_json["storage"] = component_storage_policy_to_string(component.storage);
    component_json["visibility"] = component_schema_visibility_to_string(component.visibility);
    component_json["add_on_create"] = component.add_on_create;
    component_json["stable_id"] = component.stable_id;
    component_json["fields"] = json::array();
    auto& fields = component_json["fields"];
    for (const auto& field : component.fields) {
      json field_json = json::object();
      field_json["key"] = field.key;
      field_json["member_name"] = field.member_name;
      field_json["kind"] = component_field_kind_to_string(field.kind);
      field_json["authored_required"] = field.authored_required;
      field_json["default_value"] = component_field_default_value_to_json(field.default_value);
      field_json["asset"] = field.asset ? asset_field_metadata_to_json(*field.asset) : nullptr;

      if (const auto& enumeration_opt = field.enumeration; enumeration_opt) {
        const scene::ComponentEnumRegistration& enumeration = *enumeration_opt;
        json enum_values = json::array();
        for (const scene::ComponentEnumValueRegistration& ev : enumeration.values) {
          enum_values.push_back(json{{"key", ev.key}, {"value", ev.value}});
        }
        field_json["enumeration"] =
            json{{"enum_key", enumeration.enum_key}, {"values", std::move(enum_values)}};
      } else {
        field_json["enumeration"] = nullptr;
      }
      fields.push_back(std::move(field_json));
    }
    components[component.component_key] = std::move(component_json);
  }
  return schema;
}

}  // namespace teng::engine
