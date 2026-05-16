#include "engine/scene/authoring/SceneAuthoringJson.hpp"

#include <array>
#include <compare>
#include <cstdio>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <utility>

#include "engine/scene/ComponentRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneSerialization.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace teng::engine::scene::authoring {

namespace {

using nlohmann::json;
using nlohmann::ordered_json;

[[nodiscard]] json field_default_to_json(const FrozenComponentFieldRecord& field) {
  return std::visit(
      [](const auto& v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, int64_t> ||
                      std::is_same_v<T, uint64_t> || std::is_same_v<T, float> ||
                      std::is_same_v<T, std::string>) {
          return v;
        } else if constexpr (std::is_same_v<T, ComponentDefaultVec2>) {
          return json::array({v.x, v.y});
        } else if constexpr (std::is_same_v<T, ComponentDefaultVec3>) {
          return json::array({v.x, v.y, v.z});
        } else if constexpr (std::is_same_v<T, ComponentDefaultVec4>) {
          return json::array({v.x, v.y, v.z, v.w});
        } else if constexpr (std::is_same_v<T, ComponentDefaultQuat>) {
          return json::array({v.w, v.x, v.y, v.z});
        } else if constexpr (std::is_same_v<T, ComponentDefaultMat4>) {
          return json(v.elements);
        } else if constexpr (std::is_same_v<T, ComponentDefaultAssetId> ||
                             std::is_same_v<T, ComponentDefaultEnum>) {
          return json(v.value);
        } else {
          static_assert(sizeof(T) == 0, "unhandled ComponentFieldDefaultValue alternative");
        }
      },
      field.default_value);
}

[[nodiscard]] ordered_json base_candidate(const Scene& scene,
                                          const SceneSerializationContext& serialization) {
  Result<ordered_json> scene_json = serialize_scene_to_json(scene, serialization);
  return scene_json ? std::move(*scene_json) : ordered_json::object();
}

[[nodiscard]] ordered_json* find_entity_json(ordered_json& root, EntityGuid entity) {
  const std::string guid = entity_guid_lower_hex(entity);
  auto entities = root.find("entities");
  if (entities == root.end() || !entities->is_array()) {
    return nullptr;
  }
  for (auto& entity_json : *entities) {
    if (entity_json.value("guid", "") == guid) {
      return &entity_json;
    }
  }
  return nullptr;
}

void rebuild_schema_summary(ordered_json& root, const ComponentRegistry& registry) {
  std::set<std::string> component_keys;
  if (auto entities = root.find("entities"); entities != root.end() && entities->is_array()) {
    for (const auto& entity : *entities) {
      const auto components = entity.find("components");
      if (components == entity.end() || !components->is_object()) {
        continue;
      }
      for (const auto& [component_key, payload] : components->items()) {
        (void)payload;
        component_keys.insert(component_key);
      }
    }
  }

  struct ModuleRef {
    std::string id;
    uint32_t version{};
    auto operator<=>(const ModuleRef&) const = default;
  };
  std::set<ModuleRef> modules;
  ordered_json required_components = ordered_json::object();
  for (const std::string& component_key : component_keys) {
    const FrozenComponentRecord* component = registry.find(component_key);
    if (!component) {
      continue;
    }
    required_components[component_key] = component->schema_version;
    modules.insert(ModuleRef{.id = component->module_id, .version = component->module_version});
  }

  ordered_json required_modules = ordered_json::array();
  for (const ModuleRef& module : modules) {
    required_modules.push_back(ordered_json{{"id", module.id}, {"version", module.version}});
  }
  root["schema"] = ordered_json{
      {"required_modules", std::move(required_modules)},
      {"required_components", std::move(required_components)},
  };
}

[[nodiscard]] Result<ordered_json> validate_rebuilt(
    ordered_json candidate, const SceneSerializationContext& serialization) {
  rebuild_schema_summary(candidate, serialization.component_registry());
  return validated_candidate_scene_json(serialization, candidate);
}

}  // namespace

std::string entity_guid_lower_hex(EntityGuid guid) {
  std::array<char, 17> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%016llx",
                static_cast<unsigned long long>(guid.value));
  return buffer.data();
}

ordered_json default_component_payload(const FrozenComponentRecord& component) {
  ordered_json payload = ordered_json::object();
  for (const FrozenComponentFieldRecord& field : component.fields) {
    payload[field.key] = field_default_to_json(field);
  }
  return payload;
}

Result<ordered_json> validated_candidate_scene_json(const SceneSerializationContext& serialization,
                                                    const json& candidate) {
  Result<ordered_json> canonical = canonicalize_scene_json(serialization, candidate);
  REQUIRED_OR_RETURN(canonical);
  return canonical;
}

Result<json> canonical_component_payload(const json& canonical_scene_json, EntityGuid entity,
                                         std::string_view component_key) {
  const std::string guid = entity_guid_lower_hex(entity);
  for (const auto& entity_json : canonical_scene_json.at("entities")) {
    if (entity_json.value("guid", "") == guid) {
      const auto& components = entity_json.at("components");
      const auto component = components.find(std::string{component_key});
      if (component == components.end()) {
        return make_unexpected("entity does not have component '" + std::string{component_key} +
                               "'");
      }
      return *component;
    }
  }
  return make_unexpected("entity does not exist");
}

Result<ordered_json> candidate_with_created_entity(const Scene& scene,
                                                   const SceneSerializationContext& serialization,
                                                   EntityGuid entity, std::string_view name) {
  ordered_json candidate = base_candidate(scene, serialization);
  ordered_json components = ordered_json::object();
  for (const FrozenComponentRecord& component : serialization.component_registry().components()) {
    if (component.storage == ComponentStoragePolicy::Authored && component.add_on_create) {
      components[component.component_key] = default_component_payload(component);
    }
  }

  ordered_json entity_json;
  entity_json["guid"] = entity_guid_lower_hex(entity);
  if (!name.empty()) {
    entity_json["name"] = std::string{name};
  }
  entity_json["components"] = std::move(components);
  candidate["entities"].push_back(std::move(entity_json));
  return validate_rebuilt(std::move(candidate), serialization);
}

Result<ordered_json> candidate_with_renamed_entity(const Scene& scene,
                                                   const SceneSerializationContext& serialization,
                                                   EntityGuid entity, std::string_view name) {
  ordered_json candidate = base_candidate(scene, serialization);
  ordered_json* entity_json = find_entity_json(candidate, entity);
  if (!entity_json) {
    return make_unexpected("entity does not exist");
  }
  if (name.empty()) {
    entity_json->erase("name");
  } else {
    (*entity_json)["name"] = std::string{name};
  }
  return validate_rebuilt(std::move(candidate), serialization);
}

Result<ordered_json> candidate_without_entity(const Scene& scene,
                                              const SceneSerializationContext& serialization,
                                              EntityGuid entity) {
  ordered_json candidate = base_candidate(scene, serialization);
  const std::string guid = entity_guid_lower_hex(entity);
  auto& entities = candidate["entities"];
  const auto old_size = entities.size();
  for (auto it = entities.begin(); it != entities.end();) {
    it = it->value("guid", "") == guid ? entities.erase(it) : std::next(it);
  }
  if (entities.size() == old_size) {
    return make_unexpected("entity does not exist");
  }
  return validate_rebuilt(std::move(candidate), serialization);
}

Result<ordered_json> candidate_with_component_payload(
    const Scene& scene, const SceneSerializationContext& serialization, EntityGuid entity,
    std::string_view component_key, const json& payload) {
  ordered_json candidate = base_candidate(scene, serialization);
  ordered_json* entity_json = find_entity_json(candidate, entity);
  if (!entity_json) {
    return make_unexpected("entity does not exist");
  }
  (*entity_json)["components"][std::string{component_key}] = payload;
  return validate_rebuilt(std::move(candidate), serialization);
}

Result<ordered_json> candidate_without_component(const Scene& scene,
                                                 const SceneSerializationContext& serialization,
                                                 EntityGuid entity,
                                                 std::string_view component_key) {
  ordered_json candidate = base_candidate(scene, serialization);
  ordered_json* entity_json = find_entity_json(candidate, entity);
  if (!entity_json) {
    return make_unexpected("entity does not exist");
  }
  auto& components = (*entity_json)["components"];
  if (components.erase(std::string{component_key}) == 0) {
    return make_unexpected("entity does not have component '" + std::string{component_key} + "'");
  }
  return validate_rebuilt(std::move(candidate), serialization);
}

Result<ordered_json> candidate_with_component_field(const Scene& scene,
                                                    const SceneSerializationContext& serialization,
                                                    EntityGuid entity,
                                                    std::string_view component_key,
                                                    std::string_view field_key, const json& value) {
  ordered_json candidate = base_candidate(scene, serialization);
  ordered_json* entity_json = find_entity_json(candidate, entity);
  if (!entity_json) {
    return make_unexpected("entity does not exist");
  }
  auto& components = (*entity_json)["components"];
  const auto component = components.find(std::string{component_key});
  if (component == components.end()) {
    return make_unexpected("entity does not have component '" + std::string{component_key} + "'");
  }
  (*component)[std::string{field_key}] = value;
  return validate_rebuilt(std::move(candidate), serialization);
}

}  // namespace teng::engine::scene::authoring
