#include "engine/scene/SceneSerialization.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <flecs/addons/cpp/entity.hpp>
#include <format>
#include <fstream>
#include <glm/ext/matrix_float4x4.hpp>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "core/Diagnostic.hpp"
#include "core/EAssert.hpp"
#include "core/Result.hpp"
#include "engine/scene/ComponentRegistry.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace teng {

namespace engine {

namespace {

using json = nlohmann::json;

struct EntityRecord {
  EntityGuid guid;
  std::string name;
  json components;
};

[[nodiscard]] std::string io_error(const std::filesystem::path& path, std::string_view action) {
  return "failed to " + std::string(action) + " " + path.string();
}

[[nodiscard]] Result<std::string> read_text_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return make_unexpected(io_error(path, "open"));
  }
  return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] Result<void> write_text_file(const std::filesystem::path& path,
                                           std::string_view text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return make_unexpected(io_error(path, "write"));
  }
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  return {};
}

[[nodiscard]] Result<json> parse_json_file(const std::filesystem::path& path) {
  Result<std::string> text = read_text_file(path);
  REQUIRED_OR_RETURN(text);
  try {
    return json::parse(*text);
  } catch (const json::parse_error& error) {
    return make_unexpected("failed to parse JSON scene " + path.string() + " at byte " +
                           std::to_string(error.byte) + ": " + error.what());
  }
}

void derive_local_to_world(Scene& scene) {
  scene.world().each([](const Transform& transform, LocalToWorld& local_to_world) {
    local_to_world.value = transform_to_matrix(transform);
  });
}

[[nodiscard]] Result<EntityGuid> parse_hex_guid(const json& entity, std::string_view label) {
  const auto it = entity.find("guid");
  if (it == entity.end() || !it->is_string()) {
    return make_unexpected(std::string(label) + ".guid must be a string");
  }
  const std::string text = it->get<std::string>();
  if (text.size() != 16) {
    return make_unexpected(std::string(label) +
                           ".guid must be a 16-character lowercase hex string");
  }
  uint64_t value{};
  for (const char c : text) {
    value <<= 4u;
    if (c >= '0' && c <= '9') {
      value |= static_cast<uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      value |= static_cast<uint64_t>(c - 'a' + 10);
    } else {
      return make_unexpected(std::string(label) +
                             ".guid must be a 16-character lowercase hex string");
    }
  }
  if (value == 0) {
    return make_unexpected(std::string(label) + ".guid must be non-zero");
  }
  return EntityGuid{value};
}

[[nodiscard]] Result<std::vector<EntityRecord>> parse_entity_records_v2(const json& root) {
  const auto entities_it = root.find("entities");
  if (entities_it == root.end() || !entities_it->is_array()) {
    return make_unexpected("scene JSON must contain entities array");
  }

  std::vector<EntityRecord> records;
  std::unordered_set<EntityGuid> seen;
  for (size_t i = 0; i < entities_it->size(); ++i) {
    const json& entity = (*entities_it)[i];
    const std::string label = "entities[" + std::to_string(i) + "]";
    Result<EntityGuid> guid = parse_hex_guid(entity, label);
    REQUIRED_OR_RETURN(guid);
    if (seen.contains(*guid)) {
      return make_unexpected(label + ".guid duplicates another entity");
    }
    seen.insert(*guid);

    std::string name;
    if (const auto name_it = entity.find("name"); name_it != entity.end()) {
      name = name_it->get<std::string>();
    }

    records.push_back(
        EntityRecord{.guid = *guid, .name = std::move(name), .components = entity["components"]});
  }

  std::ranges::sort(records,
                    [](const EntityRecord& a, const EntityRecord& b) { return a.guid < b.guid; });
  return records;
}

[[nodiscard]] std::string entity_guid_lower_hex(EntityGuid guid) {
  return std::format("{:016x}", guid.value);
}

[[nodiscard]] json field_default_to_json(const scene::FrozenComponentFieldRecord& field) {
  return std::visit(
      [](const auto& v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
          return json(v);
        }
        if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) {
          return json(v);
        }
        if constexpr (std::is_same_v<T, float>) {
          return json(v);
        }
        if constexpr (std::is_same_v<T, std::string>) {
          return json(v);
        }
        if constexpr (std::is_same_v<T, scene::ComponentDefaultVec2>) {
          return json::array({v.x, v.y});
        }
        if constexpr (std::is_same_v<T, scene::ComponentDefaultVec3>) {
          return json::array({v.x, v.y, v.z});
        }
        if constexpr (std::is_same_v<T, scene::ComponentDefaultVec4>) {
          return json::array({v.x, v.y, v.z, v.w});
        }
        if constexpr (std::is_same_v<T, scene::ComponentDefaultQuat>) {
          return json::array({v.w, v.x, v.y, v.z});
        }
        if constexpr (std::is_same_v<T, scene::ComponentDefaultMat4>) {
          json elements = json::array();
          for (const float f : v.elements) {
            elements.push_back(f);
          }
          return elements;
        }
        if constexpr (std::is_same_v<T, scene::ComponentDefaultAssetId>) {
          return json(v.value);
        }
        if constexpr (std::is_same_v<T, scene::ComponentDefaultEnum>) {
          return json(v.value);
        }
        return {nullptr};
      },
      field.default_value);
}

[[nodiscard]] nlohmann::ordered_json canonical_component_payload(
    const scene::FrozenComponentRecord& record, json binding_payload) {
  nlohmann::ordered_json out;
  for (const scene::FrozenComponentFieldRecord& field : record.fields) {
    const auto it = binding_payload.find(field.key);
    if (it != binding_payload.end()) {
      out[std::string{field.key}] = *it;
    } else {
      out[std::string{field.key}] = field_default_to_json(field);
    }
  }
  return out;
}

}  // namespace

Result<nlohmann::ordered_json> serialize_scene_to_json(
    const Scene& scene, const SceneSerializationContext& serialization) {
  using ordered_json = nlohmann::ordered_json;
  const scene::ComponentRegistry& registry = serialization.component_registry();

  struct SerializedEntity {
    EntityGuid guid;
    ordered_json value;
  };
  std::vector<SerializedEntity> entities;
  std::set<std::string> all_used_component_keys;

  std::vector<const scene::FrozenComponentRecord*> authored_components;
  for (const scene::FrozenComponentRecord& component : registry.components()) {
    if (component.storage == scene::ComponentStoragePolicy::Authored) {
      authored_components.push_back(&component);
    }
  }
  std::ranges::sort(authored_components, {}, &scene::FrozenComponentRecord::component_key);

  scene.world().each([&](flecs::entity entity, const EntityGuidComponent& guid_component) {
    ordered_json components = ordered_json::object();

    for (const scene::FrozenComponentRecord* record : authored_components) {
      ALWAYS_ASSERT(record->ops.has_component_fn,
                    "has_component_fn is required for component key {}", record->component_key);
      ALWAYS_ASSERT(record->ops.serialize_fn, "serialize_fn is required for component key {}",
                    record->component_key);
      if (!record->ops.has_component_fn(entity)) {
        continue;
      }
      json binding_payload = record->ops.serialize_fn(entity);
      const std::string component_key{record->component_key};
      components[component_key] = canonical_component_payload(*record, std::move(binding_payload));
      all_used_component_keys.insert(component_key);
    }

    ordered_json entity_json;
    entity_json["guid"] = entity_guid_lower_hex(guid_component.guid);
    if (const auto* name = entity.try_get<Name>(); name && !name->value.empty()) {
      entity_json["name"] = name->value;
    }
    entity_json["components"] = std::move(components);
    entities.push_back(
        SerializedEntity{.guid = guid_component.guid, .value = std::move(entity_json)});
  });

  std::ranges::sort(entities, {}, &SerializedEntity::guid);

  struct ModuleRef {
    std::string id;
    uint32_t version{};
  };
  std::vector<ModuleRef> modules;
  for (const std::string& component_key : all_used_component_keys) {
    const scene::FrozenComponentRecord* record = registry.find(component_key);
    ASSERT(record);
    const scene::FrozenModuleRecord* module = registry.find_module(record->module_id);
    ASSERT(module);
    const auto existing = std::ranges::find_if(
        modules, [&](const ModuleRef& m) { return m.id == module->module_id; });
    if (existing == modules.end()) {
      modules.push_back(ModuleRef{.id = module->module_id, .version = module->version});
    }
  }
  std::ranges::sort(modules, {}, &ModuleRef::id);

  ordered_json required_components = ordered_json::object();
  for (const std::string& component_key : all_used_component_keys) {
    const scene::FrozenComponentRecord* record = registry.find(component_key);
    ASSERT(record);
    required_components[component_key] = record->schema_version;
  }

  ordered_json required_modules = ordered_json::array();
  for (const ModuleRef& module : modules) {
    ordered_json module_json = ordered_json::object();
    module_json["id"] = module.id;
    module_json["version"] = module.version;
    required_modules.push_back(std::move(module_json));
  }

  ordered_json schema;
  schema["required_modules"] = std::move(required_modules);
  schema["required_components"] = std::move(required_components);

  ordered_json scene_obj;
  scene_obj["name"] = scene.name().empty() ? "Untitled Scene" : scene.name();

  ordered_json root;
  root["scene_format_version"] = 2;
  root["schema"] = std::move(schema);
  root["scene"] = std::move(scene_obj);
  root["entities"] = ordered_json::array();
  for (SerializedEntity& entity : entities) {
    root["entities"].push_back(std::move(entity.value));
  }
  return root;
}

Result<void> deserialize_scene_json(SceneManager& scenes,
                                    const SceneSerializationContext& serialization,
                                    const nlohmann::json& scene_json) {
  Result<void, core::DiagnosticReport> validated =
      validate_scene_file_full_report(serialization, scene_json);
  if (!validated) {
    return make_unexpected(validated.error().to_string());
  }

  const std::string name = scene_json["scene"]["name"].get<std::string>();
  Result<std::vector<EntityRecord>> records = parse_entity_records_v2(scene_json);
  REQUIRED_OR_RETURN(records);

  Scene& scene = scenes.create_scene(name);
  for (const EntityRecord& record : *records) {
    const flecs::entity entity = scene.create_entity(record.guid, record.name);
    for (const auto& [key, payload] : record.components.items()) {
      const scene::FrozenComponentRecord* component = serialization.find_authored_component(key);
      ALWAYS_ASSERT(component, "validated component key {} is missing a serialization binding",
                    key);
      ALWAYS_ASSERT(component->ops.deserialize_fn,
                    "validated component key {} is missing a deserialization binding", key);
      component->ops.deserialize_fn(entity, payload);
    }
  }

  // Temporary compatibility fixup: render extraction currently expects LocalToWorld to be current
  // immediately after load. A dedicated transform propagation/dirty system should retire this.
  derive_local_to_world(scene);
  scenes.set_active_scene(scene.id());
  return {};
}

Result<void> deserialize_scene_json2(SceneManager& scenes,
                                     const SceneSerializationContext& serialization,
                                     const nlohmann::json& scene_json) {
  return deserialize_scene_json(scenes, serialization, scene_json);
}

Result<void> save_scene_file(const Scene& scene, const SceneSerializationContext& serialization,
                             const std::filesystem::path& path) {
  Result<nlohmann::ordered_json> scene_json = serialize_scene_to_json(scene, serialization);
  REQUIRED_OR_RETURN(scene_json);
  return write_text_file(path, (*scene_json).dump(2) + "\n");
}

Result<SceneLoadResult> load_scene_file(SceneManager& scenes,
                                        const SceneSerializationContext& serialization,
                                        const std::filesystem::path& path) {
  Result<json> scene_json = parse_json_file(path);
  REQUIRED_OR_RETURN(scene_json);
  Result<void> loaded = deserialize_scene_json(scenes, serialization, *scene_json);
  REQUIRED_OR_RETURN(loaded);
  Scene* scene = scenes.active_scene();
  return SceneLoadResult{.scene_id = scene->id(), .scene = scene};
}

Result<void> validate_scene_file(const SceneSerializationContext& serialization,
                                 const std::filesystem::path& path) {
  Result<json> scene_json = parse_json_file(path);
  REQUIRED_OR_RETURN(scene_json);
  Result<void, core::DiagnosticReport> validated =
      validate_scene_file_full_report(serialization, *scene_json);
  if (!validated) {
    return make_unexpected(validated.error().to_string());
  }
  return {};
}

namespace {

using DiagnosticPath = core::DiagnosticPath;
using FieldRecord = scene::FrozenComponentFieldRecord;

[[nodiscard]] DiagnosticPath path_with_key(DiagnosticPath path, std::string_view key) {
  path.object_key(std::string{key});
  return path;
}

[[nodiscard]] DiagnosticPath path_with_index(DiagnosticPath path, size_t index) {
  path.array_index(index);
  return path;
}

void add_validation_error(core::DiagnosticReport& report, DiagnosticPath path,
                          std::string message) {
  report.add_error(core::DiagnosticCode{"scene.serialization.invalid_schema"}, std::move(path),
                   std::move(message));
}

[[nodiscard]] bool require_object(core::DiagnosticReport& report, const json& value,
                                  DiagnosticPath path, std::string_view label) {
  if (value.is_object()) {
    return true;
  }
  add_validation_error(report, std::move(path), std::string{label} + " must be an object");
  return false;
}

[[nodiscard]] bool require_array(core::DiagnosticReport& report, const json& value,
                                 DiagnosticPath path, std::string_view label) {
  if (value.is_array()) {
    return true;
  }
  add_validation_error(report, std::move(path), std::string{label} + " must be an array");
  return false;
}

[[nodiscard]] bool require_string(core::DiagnosticReport& report, const json& value,
                                  DiagnosticPath path, std::string_view label) {
  if (value.is_string()) {
    return true;
  }
  add_validation_error(report, std::move(path), std::string{label} + " must be a string");
  return false;
}

[[nodiscard]] bool require_uint(core::DiagnosticReport& report, const json& value,
                                DiagnosticPath path, std::string_view label) {
  if (value.is_number_unsigned() || (value.is_number_integer() && value.get<int64_t>() >= 0)) {
    return true;
  }
  add_validation_error(report, std::move(path),
                       std::string{label} + " must be an unsigned integer");
  return false;
}

[[nodiscard]] const json* find_required(core::DiagnosticReport& report, const json& object,
                                        std::string_view key, DiagnosticPath path,
                                        std::string_view label) {
  const auto it = object.find(key);
  if (it != object.end()) {
    return &*it;
  }
  add_validation_error(report, std::move(path), std::string{label} + " is required");
  return nullptr;
}

[[nodiscard]] bool is_fixed_lower_hex_guid(std::string_view text) {
  return text.size() == 16 && std::ranges::all_of(text, [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

[[nodiscard]] uint32_t json_uint32_value(const json& value) {
  if (value.is_number_unsigned()) {
    return value.get<uint32_t>();
  }
  return static_cast<uint32_t>(value.get<int64_t>());
}

[[nodiscard]] bool json_unsigned_fits_u32(const json& value) {
  if (value.is_number_unsigned()) {
    return value.get<uint64_t>() <= UINT32_MAX;
  }
  return value.is_number_integer() && value.get<int64_t>() >= 0 &&
         value.get<int64_t>() <= UINT32_MAX;
}

void validate_numeric_array(core::DiagnosticReport& report, const json& value, size_t expected_size,
                            DiagnosticPath path, std::string_view label) {
  if (!value.is_array() || value.size() != expected_size) {
    add_validation_error(
        report, std::move(path),
        std::string{label} + " must be a " + std::to_string(expected_size) + "-number array");
    return;
  }
  for (size_t i = 0; i < value.size(); ++i) {
    if (!value[i].is_number()) {
      add_validation_error(report, path_with_index(path, i),
                           std::string{label} + " element must be a number");
    }
  }
}

void validate_field_value(core::DiagnosticReport& report, const json& value,
                          const FieldRecord& field, DiagnosticPath path, std::string_view label) {
  using scene::ComponentFieldKind;
  switch (field.kind) {
    case ComponentFieldKind::Bool:
      if (!value.is_boolean()) {
        add_validation_error(report, std::move(path), std::string{label} + " must be a boolean");
      }
      return;
    case ComponentFieldKind::I32:
      if (!value.is_number_integer() ||
          (value.is_number_unsigned() && value.get<uint64_t>() > INT32_MAX) ||
          (!value.is_number_unsigned() &&
           (value.get<int64_t>() < INT32_MIN || value.get<int64_t>() > INT32_MAX))) {
        add_validation_error(report, std::move(path), std::string{label} + " must be an i32");
      }
      return;
    case ComponentFieldKind::U32:
      if (!json_unsigned_fits_u32(value)) {
        add_validation_error(report, std::move(path), std::string{label} + " must be a u32");
      }
      return;
    case ComponentFieldKind::F32:
      if (!value.is_number()) {
        add_validation_error(report, std::move(path), std::string{label} + " must be a number");
      }
      return;
    case ComponentFieldKind::String:
      if (!value.is_string()) {
        add_validation_error(report, std::move(path), std::string{label} + " must be a string");
      }
      return;
    case ComponentFieldKind::Vec2:
      validate_numeric_array(report, value, 2, std::move(path), label);
      return;
    case ComponentFieldKind::Vec3:
      validate_numeric_array(report, value, 3, std::move(path), label);
      return;
    case ComponentFieldKind::Vec4:
    case ComponentFieldKind::Quat:
      validate_numeric_array(report, value, 4, std::move(path), label);
      return;
    case ComponentFieldKind::Mat4:
      validate_numeric_array(report, value, 16, std::move(path), label);
      return;
    case ComponentFieldKind::AssetId:
      if (!value.is_string() || !AssetId::parse(value.get<std::string>())) {
        add_validation_error(report, std::move(path),
                             std::string{label} + " must be a valid AssetId string");
      }
      return;
    case ComponentFieldKind::Enum: {
      if (!value.is_number_integer()) {
        add_validation_error(report, std::move(path),
                             std::string{label} + " must be an integer enum discriminant");
        return;
      }
      const int64_t discriminant = value.get<int64_t>();
      const bool known_value =
          field.enumeration &&
          std::ranges::any_of(field.enumeration->values, [discriminant](const auto& enum_value) {
            return enum_value.value == discriminant;
          });
      if (!known_value) {
        add_validation_error(report, std::move(path),
                             std::string{label} + " must be a registered enum discriminant");
      }
      return;
    }
  }
}

[[nodiscard]] const FieldRecord* find_field(const scene::FrozenComponentRecord& component,
                                            std::string_view key) {
  const auto it = std::ranges::find(component.fields, key, &FieldRecord::key);
  return it == component.fields.end() ? nullptr : &*it;
}

void validate_component_payload(core::DiagnosticReport& report,
                                const SceneSerializationContext& serialization,
                                const scene::FrozenComponentRecord& component, const json& payload,
                                DiagnosticPath path) {
  if (!require_object(report, payload, path, "component payload")) {
    return;
  }

  for (const auto& [field_key, value] : payload.items()) {
    const FieldRecord* field = find_field(component, field_key);
    if (!field) {
      continue;
    }
    validate_field_value(report, value, *field, path_with_key(path, field_key),
                         "component field '" + component.component_key + "." + field_key + "'");
  }

  for (const FieldRecord& field : component.fields) {
    if (!payload.contains(field.key)) {
      add_validation_error(report, path_with_key(path, field.key),
                           "component '" + component.component_key +
                               "' is missing required field '" + field.key + "'");
    }
  }

  if (!serialization.find_authored_component(component.component_key)) {
    add_validation_error(
        report, std::move(path),
        "component '" + component.component_key + "' does not have a JSON serialization binding");
  }
}

struct SchemaUse {
  std::vector<std::string> component_keys;
  std::vector<std::string> module_ids;
};

void add_unique_sorted(std::vector<std::string>& values, std::string value) {
  if (std::ranges::find(values, value) == values.end()) {
    values.push_back(std::move(value));
    std::ranges::sort(values);
  }
}

void validate_schema_summary(core::DiagnosticReport& report,
                             const scene::ComponentRegistry& registry, const json& required_modules,
                             const json& required_components, const SchemaUse& use) {
  std::vector<std::string> declared_modules;
  for (size_t i = 0; i < required_modules.size(); ++i) {
    const json& module = required_modules[i];
    const DiagnosticPath module_path =
        path_with_index(DiagnosticPath{}.object_key("schema").object_key("required_modules"), i);
    if (!require_object(report, module, module_path, "schema.required_modules[]")) {
      continue;
    }
    const json* id = find_required(report, module, "id", path_with_key(module_path, "id"),
                                   "schema.required_modules[].id");
    const json* version =
        find_required(report, module, "version", path_with_key(module_path, "version"),
                      "schema.required_modules[].version");
    if (!id || !version ||
        !require_string(report, *id, path_with_key(module_path, "id"),
                        "schema.required_modules[].id") ||
        !require_uint(report, *version, path_with_key(module_path, "version"),
                      "schema.required_modules[].version")) {
      continue;
    }

    const std::string module_id = id->get<std::string>();
    if (std::ranges::find(declared_modules, module_id) != declared_modules.end()) {
      add_validation_error(report, path_with_key(module_path, "id"),
                           "required module '" + module_id + "' is duplicated");
    }
    add_unique_sorted(declared_modules, module_id);
    const scene::FrozenModuleRecord* record = registry.find_module(module_id);
    if (!record) {
      add_validation_error(report, path_with_key(module_path, "id"),
                           "required module '" + module_id + "' is not registered");
    } else if (!json_unsigned_fits_u32(*version) ||
               json_uint32_value(*version) != record->version) {
      add_validation_error(report, path_with_key(module_path, "version"),
                           "required module '" + module_id + "' has unsupported version");
    }
  }

  for (const std::string& module_id : use.module_ids) {
    if (std::ranges::find(declared_modules, module_id) == declared_modules.end()) {
      add_validation_error(report,
                           DiagnosticPath{}
                               .object_key("schema")
                               .object_key("required_modules")
                               .object_key(module_id),
                           "required module '" + module_id + "' is missing");
    }
  }

  std::vector<std::string> declared_components;
  for (const auto& [component_key, version] : required_components.items()) {
    add_unique_sorted(declared_components, component_key);
    const DiagnosticPath version_path = DiagnosticPath{}
                                            .object_key("schema")
                                            .object_key("required_components")
                                            .object_key(component_key);
    if (!require_uint(report, version, version_path, "schema.required_components value")) {
      continue;
    }
    const scene::FrozenComponentRecord* record = registry.find(component_key);
    if (!record) {
      add_validation_error(report, version_path,
                           "required component '" + component_key + "' is not registered");
    } else if (!json_unsigned_fits_u32(version) ||
               json_uint32_value(version) != record->schema_version) {
      add_validation_error(
          report, version_path,
          "required component '" + component_key + "' has unsupported schema version");
    }
  }

  for (const std::string& component_key : use.component_keys) {
    if (std::ranges::find(declared_components, component_key) == declared_components.end()) {
      add_validation_error(report,
                           DiagnosticPath{}
                               .object_key("schema")
                               .object_key("required_components")
                               .object_key(component_key),
                           "required component '" + component_key + "' is missing");
    }
  }
}

}  // namespace

Result<void, core::DiagnosticReport> validate_scene_file_full_report(
    const SceneSerializationContext& serialization, const nlohmann::json& scene_json) {
  core::DiagnosticReport report;
  const scene::ComponentRegistry& registry = serialization.component_registry();

  if (!require_object(report, scene_json, DiagnosticPath{}, "scene JSON")) {
    return make_unexpected(report);
  }

  const json* version =
      find_required(report, scene_json, "scene_format_version",
                    DiagnosticPath{}.object_key("scene_format_version"), "scene_format_version");
  if (version) {
    if (!json_unsigned_fits_u32(*version) || json_uint32_value(*version) != 2) {
      add_validation_error(report, DiagnosticPath{}.object_key("scene_format_version"),
                           "scene_format_version must be 2");
    }
  }

  const json* schema =
      find_required(report, scene_json, "schema", DiagnosticPath{}.object_key("schema"), "schema");
  const json* scene =
      find_required(report, scene_json, "scene", DiagnosticPath{}.object_key("scene"), "scene");
  const json* entities = find_required(report, scene_json, "entities",
                                       DiagnosticPath{}.object_key("entities"), "entities");

  const json* required_modules = nullptr;
  const json* required_components = nullptr;
  if (schema && require_object(report, *schema, DiagnosticPath{}.object_key("schema"), "schema")) {
    required_modules =
        find_required(report, *schema, "required_modules",
                      DiagnosticPath{}.object_key("schema").object_key("required_modules"),
                      "schema.required_modules");
    required_components =
        find_required(report, *schema, "required_components",
                      DiagnosticPath{}.object_key("schema").object_key("required_components"),
                      "schema.required_components");
    if (required_modules) {
      (void)require_array(report, *required_modules,
                          DiagnosticPath{}.object_key("schema").object_key("required_modules"),
                          "schema.required_modules");
    }
    if (required_components) {
      (void)require_object(report, *required_components,
                           DiagnosticPath{}.object_key("schema").object_key("required_components"),
                           "schema.required_components");
    }
  }

  if (scene && require_object(report, *scene, DiagnosticPath{}.object_key("scene"), "scene")) {
    const json* name =
        find_required(report, *scene, "name",
                      DiagnosticPath{}.object_key("scene").object_key("name"), "scene.name");
    if (name &&
        require_string(report, *name, DiagnosticPath{}.object_key("scene").object_key("name"),
                       "scene.name") &&
        name->get<std::string>().empty()) {
      add_validation_error(report, DiagnosticPath{}.object_key("scene").object_key("name"),
                           "scene.name must be non-empty");
    }
  }

  SchemaUse use;
  std::unordered_set<std::string> seen_guids;
  if (entities &&
      require_array(report, *entities, DiagnosticPath{}.object_key("entities"), "entities")) {
    for (size_t i = 0; i < entities->size(); ++i) {
      const json& entity = (*entities)[i];
      const DiagnosticPath entity_path = DiagnosticPath{}.object_key("entities").array_index(i);
      if (!require_object(report, entity, entity_path, "entity")) {
        continue;
      }

      const json* guid =
          find_required(report, entity, "guid", path_with_key(entity_path, "guid"), "entity.guid");
      if (guid &&
          require_string(report, *guid, path_with_key(entity_path, "guid"), "entity.guid")) {
        const std::string guid_text = guid->get<std::string>();
        if (!is_fixed_lower_hex_guid(guid_text) || guid_text == "0000000000000000") {
          add_validation_error(report, path_with_key(entity_path, "guid"),
                               "entity.guid must be a non-zero 16-character lowercase hex string");
        } else if (!seen_guids.insert(guid_text).second) {
          add_validation_error(report, path_with_key(entity_path, "guid"),
                               "entity.guid duplicates another entity");
        }
      }

      if (const auto name = entity.find("name"); name != entity.end()) {
        if (require_string(report, *name, path_with_key(entity_path, "name"), "entity.name") &&
            name->get<std::string>().empty()) {
          add_validation_error(report, path_with_key(entity_path, "name"),
                               "entity.name must be non-empty when present");
        }
      }

      const json* components =
          find_required(report, entity, "components", path_with_key(entity_path, "components"),
                        "entity.components");
      if (!components ||
          !require_object(report, *components, path_with_key(entity_path, "components"),
                          "entity.components")) {
        continue;
      }

      for (const auto& [component_key, payload] : components->items()) {
        const DiagnosticPath component_path =
            path_with_key(path_with_key(entity_path, "components"), component_key);
        const scene::FrozenComponentRecord* component = registry.find(component_key);
        if (!component) {
          add_validation_error(report, component_path,
                               "entity component '" + component_key + "' is not registered");
          continue;
        }
        if (component->storage != scene::ComponentStoragePolicy::Authored) {
          add_validation_error(report, component_path,
                               "entity component '" + component_key + "' is not authored");
          continue;
        }

        add_unique_sorted(use.component_keys, component->component_key);
        add_unique_sorted(use.module_ids, component->module_id);
        validate_component_payload(report, serialization, *component, payload, component_path);
      }
    }
  }

  if (required_modules && required_modules->is_array() && required_components &&
      required_components->is_object()) {
    validate_schema_summary(report, registry, *required_modules, *required_components, use);
  }

  if (report.has_errors()) {
    return make_unexpected(report);
  }
  return {};
}

Result<nlohmann::ordered_json> canonicalize_scene_json(
    const SceneSerializationContext& serialization, const nlohmann::json& scene_json) {
  Result<void, core::DiagnosticReport> validated =
      validate_scene_file_full_report(serialization, scene_json);
  if (!validated) {
    return make_unexpected(validated.error().to_string());
  }

  Result<std::vector<EntityRecord>> records = parse_entity_records_v2(scene_json);
  REQUIRED_OR_RETURN(records);

  nlohmann::ordered_json root;
  root["scene_format_version"] = 2;

  std::set<std::string> used_component_keys;
  nlohmann::ordered_json entities = nlohmann::ordered_json::array();
  for (const EntityRecord& record : *records) {
    nlohmann::ordered_json entity_json;
    entity_json["guid"] = entity_guid_lower_hex(record.guid);
    if (!record.name.empty()) {
      entity_json["name"] = record.name;
    }

    nlohmann::ordered_json components = nlohmann::ordered_json::object();
    std::vector<std::string> component_keys;
    for (const auto& [key, payload] : record.components.items()) {
      component_keys.push_back(key);
    }
    std::ranges::sort(component_keys);
    for (const std::string& key : component_keys) {
      const scene::FrozenComponentRecord* component = serialization.find_authored_component(key);
      ALWAYS_ASSERT(component, "validated component key {} is missing a serialization binding", key);
      components[key] = canonical_component_payload(*component, record.components.at(key));
      used_component_keys.insert(key);
    }
    entity_json["components"] = std::move(components);
    entities.push_back(std::move(entity_json));
  }

  struct ModuleRef {
    std::string id;
    uint32_t version{};
  };
  std::vector<ModuleRef> modules;
  for (const std::string& component_key : used_component_keys) {
    const scene::FrozenComponentRecord* component = serialization.find_authored_component(component_key);
    ASSERT(component);
    if (std::ranges::none_of(modules, [&](const ModuleRef& module) {
          return module.id == component->module_id;
        })) {
      modules.push_back(ModuleRef{.id = component->module_id, .version = component->module_version});
    }
  }
  std::ranges::sort(modules, {}, &ModuleRef::id);

  nlohmann::ordered_json required_modules = nlohmann::ordered_json::array();
  for (const ModuleRef& module : modules) {
    nlohmann::ordered_json module_json;
    module_json["id"] = module.id;
    module_json["version"] = module.version;
    required_modules.push_back(std::move(module_json));
  }

  nlohmann::ordered_json required_components = nlohmann::ordered_json::object();
  for (const std::string& component_key : used_component_keys) {
    const scene::FrozenComponentRecord* component = serialization.find_authored_component(component_key);
    ASSERT(component);
    required_components[component_key] = component->schema_version;
  }

  nlohmann::ordered_json schema;
  schema["required_modules"] = std::move(required_modules);
  schema["required_components"] = std::move(required_components);

  nlohmann::ordered_json scene;
  scene["name"] = scene_json["scene"]["name"].get<std::string>();

  root["schema"] = std::move(schema);
  root["scene"] = std::move(scene);
  root["entities"] = std::move(entities);
  return root;
}

Result<void> migrate_scene_file(const std::filesystem::path& input_path,
                                const std::filesystem::path& output_path) {
  (void)input_path;
  (void)output_path;
  return make_unexpected(
      "scene migration is not supported until a JSON v2 migration target exists");
}

}  // namespace engine

}  // namespace teng
