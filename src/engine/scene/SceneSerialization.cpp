#include "engine/scene/SceneSerialization.hpp"

#include <algorithm>
#include <array>
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

#include "core/ComponentRegistry.hpp"
#include "core/Diagnostic.hpp"
#include "core/EAssert.hpp"
#include "core/Result.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace teng {

namespace engine {

namespace {

using json = nlohmann::json;

constexpr uint64_t k_max_json_safe_integer = 9007199254740991ull;

enum ComponentBit : uint32_t {
  k_transform_bit = 1u << 0u,
  k_camera_bit = 1u << 1u,
  k_directional_light_bit = 1u << 2u,
  k_mesh_renderable_bit = 1u << 3u,
  k_sprite_renderable_bit = 1u << 4u,
};

struct EntityRecord {
  EntityGuid guid;
  std::string name;
  json components;
};

struct CookEntity {
  EntityGuid guid;
  uint32_t name_index{};
  uint32_t component_mask{};
};

struct CookComponent {
  uint32_t entity_index{};
  uint32_t key_index{};
  uint64_t blob_offset{};
  uint64_t blob_size{};
};

struct ComponentCodec {
  std::string_view key;
  uint32_t bit;
  Result<void> (*validate)(const json& payload);
  bool (*serialize)(flecs::entity entity, json& components);
  Result<void> (*deserialize)(flecs::entity entity, const json& payload);
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

[[nodiscard]] Result<void> ensure_object(const json& value, std::string_view label) {
  if (!value.is_object()) {
    return make_unexpected(std::string(label) + " must be an object");
  }
  return {};
}

[[nodiscard]] Result<void> ensure_allowed_keys(const json& object,
                                               std::span<const std::string_view> allowed,
                                               std::string_view label) {
  for (const auto& [key, value] : object.items()) {
    (void)value;
    if (std::ranges::find(allowed, key) == allowed.end()) {
      return make_unexpected(std::string(label) + " has unknown key '" + key + "'");
    }
  }
  return {};
}

[[nodiscard]] Result<float> required_float(const json& object, std::string_view field,
                                           std::string_view label) {
  const auto it = object.find(field);
  if (it == object.end() || !it->is_number()) {
    return make_unexpected(std::string(label) + "." + std::string(field) + " must be a number");
  }
  return it->get<float>();
}

[[nodiscard]] Result<int> required_int(const json& object, std::string_view field,
                                       std::string_view label) {
  const auto it = object.find(field);
  if (it == object.end() || !it->is_number_integer()) {
    return make_unexpected(std::string(label) + "." + std::string(field) + " must be an integer");
  }
  return it->get<int>();
}

[[nodiscard]] Result<bool> required_bool(const json& object, std::string_view field,
                                         std::string_view label) {
  const auto it = object.find(field);
  if (it == object.end() || !it->is_boolean()) {
    return make_unexpected(std::string(label) + "." + std::string(field) + " must be a boolean");
  }
  return it->get<bool>();
}

[[nodiscard]] Result<std::string> required_string(const json& object, std::string_view field,
                                                  std::string_view label) {
  const auto it = object.find(field);
  if (it == object.end() || !it->is_string()) {
    return make_unexpected(std::string(label) + "." + std::string(field) + " must be a string");
  }
  return it->get<std::string>();
}

[[nodiscard]] Result<std::array<float, 3>> required_vec3(const json& object, std::string_view field,
                                                         std::string_view label) {
  const auto it = object.find(field);
  if (it == object.end() || !it->is_array() || it->size() != 3) {
    return make_unexpected(std::string(label) + "." + std::string(field) +
                           " must be a 3-number array");
  }
  std::array<float, 3> out{};
  for (size_t i = 0; i < out.size(); ++i) {
    if (!(*it)[i].is_number()) {
      return make_unexpected(std::string(label) + "." + std::string(field) +
                             " must be a 3-number array");
    }
    out[i] = (*it)[i].get<float>();
  }
  return out;
}

[[nodiscard]] Result<std::array<float, 4>> required_vec4(const json& object, std::string_view field,
                                                         std::string_view label) {
  const auto it = object.find(field);
  if (it == object.end() || !it->is_array() || it->size() != 4) {
    return make_unexpected(std::string(label) + "." + std::string(field) +
                           " must be a 4-number array");
  }
  std::array<float, 4> out{};
  for (size_t i = 0; i < out.size(); ++i) {
    if (!(*it)[i].is_number()) {
      return make_unexpected(std::string(label) + "." + std::string(field) +
                             " must be a 4-number array");
    }
    out[i] = (*it)[i].get<float>();
  }
  return out;
}

[[nodiscard]] json vec3_json(const glm::vec3& value) {
  return json::array({value.x, value.y, value.z});
}

[[nodiscard]] json vec4_json(const glm::vec4& value) {
  return json::array({value.x, value.y, value.z, value.w});
}

[[nodiscard]] json quat_json(const glm::quat& value) {
  return json::array({value.w, value.x, value.y, value.z});
}

[[nodiscard]] Result<Transform> parse_transform(const json& payload) {
  constexpr std::array<std::string_view, 3> keys{"rotation", "scale", "translation"};
  Result<void> object_ok = ensure_object(payload, "transform");
  REQUIRED_OR_RETURN(object_ok);
  Result<void> keys_ok = ensure_allowed_keys(payload, keys, "transform");
  REQUIRED_OR_RETURN(keys_ok);
  Result<std::array<float, 3>> translation = required_vec3(payload, "translation", "transform");
  REQUIRED_OR_RETURN(translation);
  Result<std::array<float, 4>> rotation = required_vec4(payload, "rotation", "transform");
  REQUIRED_OR_RETURN(rotation);
  Result<std::array<float, 3>> scale = required_vec3(payload, "scale", "transform");
  REQUIRED_OR_RETURN(scale);
  return Transform{.translation = {(*translation)[0], (*translation)[1], (*translation)[2]},
                   .rotation = {(*rotation)[0], (*rotation)[1], (*rotation)[2], (*rotation)[3]},
                   .scale = {(*scale)[0], (*scale)[1], (*scale)[2]}};
}

[[nodiscard]] json transform_json(const Transform& transform) {
  return json{{"rotation", quat_json(transform.rotation)},
              {"scale", vec3_json(transform.scale)},
              {"translation", vec3_json(transform.translation)}};
}

[[nodiscard]] Result<Camera> parse_camera(const json& payload) {
  constexpr std::array<std::string_view, 4> keys{"fov_y", "primary", "z_far", "z_near"};
  Result<void> object_ok = ensure_object(payload, "camera");
  REQUIRED_OR_RETURN(object_ok);
  Result<void> keys_ok = ensure_allowed_keys(payload, keys, "camera");
  REQUIRED_OR_RETURN(keys_ok);
  Result<float> fov_y = required_float(payload, "fov_y", "camera");
  REQUIRED_OR_RETURN(fov_y);
  Result<float> z_near = required_float(payload, "z_near", "camera");
  REQUIRED_OR_RETURN(z_near);
  Result<float> z_far = required_float(payload, "z_far", "camera");
  REQUIRED_OR_RETURN(z_far);
  Result<bool> primary = required_bool(payload, "primary", "camera");
  REQUIRED_OR_RETURN(primary);
  return Camera{.fov_y = *fov_y, .z_near = *z_near, .z_far = *z_far, .primary = *primary};
}

[[nodiscard]] json camera_json(const Camera& camera) {
  return json{{"fov_y", camera.fov_y},
              {"primary", camera.primary},
              {"z_far", camera.z_far},
              {"z_near", camera.z_near}};
}

[[nodiscard]] Result<DirectionalLight> parse_directional_light(const json& payload) {
  constexpr std::array<std::string_view, 3> keys{"color", "direction", "intensity"};
  Result<void> object_ok = ensure_object(payload, "directional_light");
  REQUIRED_OR_RETURN(object_ok);
  Result<void> keys_ok = ensure_allowed_keys(payload, keys, "directional_light");
  REQUIRED_OR_RETURN(keys_ok);
  Result<std::array<float, 3>> direction = required_vec3(payload, "direction", "directional_light");
  REQUIRED_OR_RETURN(direction);
  Result<std::array<float, 3>> color = required_vec3(payload, "color", "directional_light");
  REQUIRED_OR_RETURN(color);
  Result<float> intensity = required_float(payload, "intensity", "directional_light");
  REQUIRED_OR_RETURN(intensity);
  return DirectionalLight{.direction = {(*direction)[0], (*direction)[1], (*direction)[2]},
                          .color = {(*color)[0], (*color)[1], (*color)[2]},
                          .intensity = *intensity};
}

[[nodiscard]] json directional_light_json(const DirectionalLight& light) {
  return json{{"color", vec3_json(light.color)},
              {"direction", vec3_json(light.direction)},
              {"intensity", light.intensity}};
}

[[nodiscard]] Result<AssetId> parse_asset_id_field(const json& payload, std::string_view field,
                                                   std::string_view label) {
  Result<std::string> text = required_string(payload, field, label);
  REQUIRED_OR_RETURN(text);
  std::optional<AssetId> parsed = AssetId::parse(*text);
  if (!parsed) {
    return make_unexpected(std::string(label) + "." + std::string(field) +
                           " must be a valid AssetId");
  }
  return *parsed;
}

[[nodiscard]] Result<MeshRenderable> parse_mesh_renderable(const json& payload) {
  constexpr std::array<std::string_view, 1> keys{"model"};
  Result<void> object_ok = ensure_object(payload, "mesh_renderable");
  REQUIRED_OR_RETURN(object_ok);
  Result<void> keys_ok = ensure_allowed_keys(payload, keys, "mesh_renderable");
  REQUIRED_OR_RETURN(keys_ok);
  Result<AssetId> model = parse_asset_id_field(payload, "model", "mesh_renderable");
  REQUIRED_OR_RETURN(model);
  return MeshRenderable{.model = *model};
}

[[nodiscard]] json mesh_renderable_json(const MeshRenderable& mesh) {
  return json{{"model", mesh.model.to_string()}};
}

[[nodiscard]] Result<SpriteRenderable> parse_sprite_renderable(const json& payload) {
  constexpr std::array<std::string_view, 4> keys{"sorting_layer", "sorting_order", "texture",
                                                 "tint"};
  Result<void> object_ok = ensure_object(payload, "sprite_renderable");
  REQUIRED_OR_RETURN(object_ok);
  Result<void> keys_ok = ensure_allowed_keys(payload, keys, "sprite_renderable");
  REQUIRED_OR_RETURN(keys_ok);
  Result<AssetId> texture = parse_asset_id_field(payload, "texture", "sprite_renderable");
  REQUIRED_OR_RETURN(texture);
  Result<std::array<float, 4>> tint = required_vec4(payload, "tint", "sprite_renderable");
  REQUIRED_OR_RETURN(tint);
  Result<int> sorting_layer = required_int(payload, "sorting_layer", "sprite_renderable");
  REQUIRED_OR_RETURN(sorting_layer);
  Result<int> sorting_order = required_int(payload, "sorting_order", "sprite_renderable");
  REQUIRED_OR_RETURN(sorting_order);
  return SpriteRenderable{.texture = *texture,
                          .tint = {(*tint)[0], (*tint)[1], (*tint)[2], (*tint)[3]},
                          .sorting_layer = *sorting_layer,
                          .sorting_order = *sorting_order};
}

[[nodiscard]] json sprite_renderable_json(const SpriteRenderable& sprite) {
  return json{{"sorting_layer", sprite.sorting_layer},
              {"sorting_order", sprite.sorting_order},
              {"texture", sprite.texture.to_string()},
              {"tint", vec4_json(sprite.tint)}};
}

[[nodiscard]] Result<void> validate_transform_payload(const json& payload) {
  Result<Transform> parsed = parse_transform(payload);
  REQUIRED_OR_RETURN(parsed);
  return {};
}

[[nodiscard]] Result<void> validate_camera_payload(const json& payload) {
  Result<Camera> parsed = parse_camera(payload);
  REQUIRED_OR_RETURN(parsed);
  return {};
}

[[nodiscard]] Result<void> validate_directional_light_payload(const json& payload) {
  Result<DirectionalLight> parsed = parse_directional_light(payload);
  REQUIRED_OR_RETURN(parsed);
  return {};
}

[[nodiscard]] Result<void> validate_mesh_renderable_payload(const json& payload) {
  Result<MeshRenderable> parsed = parse_mesh_renderable(payload);
  REQUIRED_OR_RETURN(parsed);
  return {};
}

[[nodiscard]] Result<void> validate_sprite_renderable_payload(const json& payload) {
  Result<SpriteRenderable> parsed = parse_sprite_renderable(payload);
  REQUIRED_OR_RETURN(parsed);
  return {};
}

bool serialize_transform_payload(flecs::entity entity, json& components) {
  const auto* transform = entity.try_get<Transform>();
  if (!transform) {
    return false;
  }
  components["transform"] = transform_json(*transform);
  return true;
}

bool serialize_camera_payload(flecs::entity entity, json& components) {
  const auto* camera = entity.try_get<Camera>();
  if (!camera) {
    return false;
  }
  components["camera"] = camera_json(*camera);
  return true;
}

bool serialize_directional_light_payload(flecs::entity entity, json& components) {
  const auto* light = entity.try_get<DirectionalLight>();
  if (!light) {
    return false;
  }
  components["directional_light"] = directional_light_json(*light);
  return true;
}

bool serialize_mesh_renderable_payload(flecs::entity entity, json& components) {
  const auto* mesh = entity.try_get<MeshRenderable>();
  if (!mesh) {
    return false;
  }
  components["mesh_renderable"] = mesh_renderable_json(*mesh);
  return true;
}

bool serialize_sprite_renderable_payload(flecs::entity entity, json& components) {
  const auto* sprite = entity.try_get<SpriteRenderable>();
  if (!sprite) {
    return false;
  }
  components["sprite_renderable"] = sprite_renderable_json(*sprite);
  return true;
}

[[nodiscard]] Result<void> deserialize_transform_payload(flecs::entity entity,
                                                         const json& payload) {
  Result<Transform> component = parse_transform(payload);
  REQUIRED_OR_RETURN(component);
  entity.set<Transform>(*component);
  return {};
}

[[nodiscard]] Result<void> deserialize_camera_payload(flecs::entity entity, const json& payload) {
  Result<Camera> component = parse_camera(payload);
  REQUIRED_OR_RETURN(component);
  entity.set<Camera>(*component);
  return {};
}

[[nodiscard]] Result<void> deserialize_directional_light_payload(flecs::entity entity,
                                                                 const json& payload) {
  Result<DirectionalLight> component = parse_directional_light(payload);
  REQUIRED_OR_RETURN(component);
  entity.set<DirectionalLight>(*component);
  return {};
}

[[nodiscard]] Result<void> deserialize_mesh_renderable_payload(flecs::entity entity,
                                                               const json& payload) {
  Result<MeshRenderable> component = parse_mesh_renderable(payload);
  REQUIRED_OR_RETURN(component);
  entity.set<MeshRenderable>(*component);
  return {};
}

[[nodiscard]] Result<void> deserialize_sprite_renderable_payload(flecs::entity entity,
                                                                 const json& payload) {
  Result<SpriteRenderable> component = parse_sprite_renderable(payload);
  REQUIRED_OR_RETURN(component);
  entity.set<SpriteRenderable>(*component);
  return {};
}

[[nodiscard]] std::span<const ComponentCodec> component_codecs() {
  static constexpr std::array<ComponentCodec, 5> codecs{
      ComponentCodec{.key = "transform",
                     .bit = k_transform_bit,
                     .validate = validate_transform_payload,
                     .serialize = serialize_transform_payload,
                     .deserialize = deserialize_transform_payload},
      ComponentCodec{.key = "camera",
                     .bit = k_camera_bit,
                     .validate = validate_camera_payload,
                     .serialize = serialize_camera_payload,
                     .deserialize = deserialize_camera_payload},
      ComponentCodec{.key = "directional_light",
                     .bit = k_directional_light_bit,
                     .validate = validate_directional_light_payload,
                     .serialize = serialize_directional_light_payload,
                     .deserialize = deserialize_directional_light_payload},
      ComponentCodec{.key = "mesh_renderable",
                     .bit = k_mesh_renderable_bit,
                     .validate = validate_mesh_renderable_payload,
                     .serialize = serialize_mesh_renderable_payload,
                     .deserialize = deserialize_mesh_renderable_payload},
      ComponentCodec{.key = "sprite_renderable",
                     .bit = k_sprite_renderable_bit,
                     .validate = validate_sprite_renderable_payload,
                     .serialize = serialize_sprite_renderable_payload,
                     .deserialize = deserialize_sprite_renderable_payload},
  };
  return codecs;
}

[[nodiscard]] const ComponentCodec* find_component_codec(std::string_view key) {
  for (const ComponentCodec& codec : component_codecs()) {
    if (codec.key == key) {
      return &codec;
    }
  }
  return nullptr;
}

void derive_local_to_world(Scene& scene) {
  scene.world().each([](const Transform& transform, LocalToWorld& local_to_world) {
    local_to_world.value = transform_to_matrix(transform);
  });
}

[[nodiscard]] Result<EntityGuid> parse_guid(const json& entity, std::string_view label) {
  const auto it = entity.find("guid");
  if (it == entity.end() || !it->is_number_unsigned()) {
    return make_unexpected(std::string(label) + ".guid must be an unsigned integer");
  }
  const uint64_t value = it->get<uint64_t>();
  if (value == 0 || value > k_max_json_safe_integer) {
    return make_unexpected(std::string(label) + ".guid must be in the JSON safe integer range");
  }
  return EntityGuid{value};
}

[[nodiscard]] Result<std::vector<EntityRecord>> parse_entity_records(const json& root) {
  const auto entities_it = root.find("entities");
  if (entities_it == root.end() || !entities_it->is_array()) {
    return make_unexpected("scene JSON must contain entities array");
  }

  std::vector<EntityRecord> records;
  std::unordered_set<EntityGuid> seen;
  for (size_t i = 0; i < entities_it->size(); ++i) {
    const json& entity = (*entities_it)[i];
    const std::string label = "entities[" + std::to_string(i) + "]";
    Result<void> object_ok = ensure_object(entity, label);
    REQUIRED_OR_RETURN(object_ok);
    constexpr std::array<std::string_view, 3> entity_keys{"components", "guid", "name"};
    Result<void> keys_ok = ensure_allowed_keys(entity, entity_keys, label);
    REQUIRED_OR_RETURN(keys_ok);

    Result<EntityGuid> guid = parse_guid(entity, label);
    REQUIRED_OR_RETURN(guid);
    if (seen.contains(*guid)) {
      return make_unexpected(label + ".guid duplicates another entity");
    }
    seen.insert(*guid);

    std::string name;
    if (const auto name_it = entity.find("name"); name_it != entity.end()) {
      if (!name_it->is_string()) {
        return make_unexpected(label + ".name must be a string");
      }
      name = name_it->get<std::string>();
    }

    const auto components_it = entity.find("components");
    if (components_it == entity.end() || !components_it->is_object()) {
      return make_unexpected(label + ".components must be an object");
    }
    records.push_back(
        EntityRecord{.guid = *guid, .name = std::move(name), .components = *components_it});
  }

  std::ranges::sort(records,
                    [](const EntityRecord& a, const EntityRecord& b) { return a.guid < b.guid; });
  return records;
}

[[nodiscard]] Result<void> validate_envelope(const json& root) {
  Result<void> object_ok = ensure_object(root, "scene JSON");
  REQUIRED_OR_RETURN(object_ok);
  constexpr std::array<std::string_view, 3> top_keys{"entities", "registry_version", "scene"};
  Result<void> keys_ok = ensure_allowed_keys(root, top_keys, "scene JSON");
  REQUIRED_OR_RETURN(keys_ok);

  const auto version_it = root.find("registry_version");
  if (version_it == root.end() || !version_it->is_number_integer() ||
      version_it->get<int>() != k_scene_registry_version) {
    return make_unexpected("scene JSON registry_version must be " +
                           std::to_string(k_scene_registry_version));
  }

  const auto scene_it = root.find("scene");
  if (scene_it == root.end()) {
    return make_unexpected("scene JSON must contain scene object");
  }
  Result<void> scene_ok = ensure_object(*scene_it, "scene");
  REQUIRED_OR_RETURN(scene_ok);
  constexpr std::array<std::string_view, 1> scene_keys{"name"};
  Result<void> scene_keys_ok = ensure_allowed_keys(*scene_it, scene_keys, "scene");
  REQUIRED_OR_RETURN(scene_keys_ok);
  Result<std::string> name = required_string(*scene_it, "name", "scene");
  REQUIRED_OR_RETURN(name);

  Result<std::vector<EntityRecord>> records = parse_entity_records(root);
  REQUIRED_OR_RETURN(records);
  for (const EntityRecord& record : *records) {
    for (const auto& [key, payload] : record.components.items()) {
      if (key == "local_to_world") {
        return make_unexpected("entity " + std::to_string(record.guid.value) +
                               " has forbidden component local_to_world");
      }
      const ComponentCodec* codec = find_component_codec(key);
      if (!codec) {
        return make_unexpected("entity " + std::to_string(record.guid.value) +
                               " has unknown component '" + key + "'");
      }
      Result<void> payload_ok = codec->validate(payload);
      REQUIRED_OR_RETURN(payload_ok);
    }
  }
  return {};
}

[[nodiscard]] std::string entity_guid_lower_hex(EntityGuid guid) {
  return std::format("{:016x}", guid.value);
}

[[nodiscard]] json field_default_to_json(const core::ComponentFieldRegistration& field) {
  if (!field.default_value) {
    return {nullptr};
  }
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
        if constexpr (std::is_same_v<T, core::ComponentDefaultVec2>) {
          return json::array({v.x, v.y});
        }
        if constexpr (std::is_same_v<T, core::ComponentDefaultVec3>) {
          return json::array({v.x, v.y, v.z});
        }
        if constexpr (std::is_same_v<T, core::ComponentDefaultVec4>) {
          return json::array({v.x, v.y, v.z, v.w});
        }
        if constexpr (std::is_same_v<T, core::ComponentDefaultQuat>) {
          return json::array({v.w, v.x, v.y, v.z});
        }
        if constexpr (std::is_same_v<T, core::ComponentDefaultMat4>) {
          json elements = json::array();
          for (const float f : v.elements) {
            elements.push_back(f);
          }
          return elements;
        }
        if constexpr (std::is_same_v<T, core::ComponentDefaultAssetId>) {
          return json(v.value);
        }
        if constexpr (std::is_same_v<T, core::ComponentDefaultEnum>) {
          return json(v.key);
        }
        return {nullptr};
      },
      *field.default_value);
}

[[nodiscard]] nlohmann::ordered_json canonical_component_payload(
    const core::FrozenComponentRecord& record, json binding_payload) {
  nlohmann::ordered_json out;
  for (const core::ComponentFieldRegistration& field : record.fields) {
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
  const core::ComponentRegistry& registry = serialization.component_registry();

  struct SerializedEntity {
    EntityGuid guid;
    ordered_json value;
  };
  std::vector<SerializedEntity> entities;
  std::set<std::string> all_used_component_keys;

  std::vector<const ComponentSerializationBinding*> sorted_bindings;
  sorted_bindings.reserve(serialization.component_bindings.size());
  for (const ComponentSerializationBinding& binding : serialization.component_bindings) {
    sorted_bindings.push_back(&binding);
  }
  std::ranges::sort(sorted_bindings, {}, &ComponentSerializationBinding::component_key);

  scene.world().each([&](flecs::entity entity, const EntityGuidComponent& guid_component) {
    ordered_json components = ordered_json::object();

    for (const ComponentSerializationBinding* binding : sorted_bindings) {
      ALWAYS_ASSERT(binding->has_component_fn, "has_component_fn is required for component key {}",
                    binding->component_key);
      if (!binding->has_component_fn(entity) || !binding->serialize_fn) {
        continue;
      }
      const core::FrozenComponentRecord* record = registry.find(binding->component_key);
      ASSERT(record);
      ASSERT(record->storage == core::ComponentStoragePolicy::Authored);
      json binding_payload = binding->serialize_fn(entity);
      const std::string component_key{binding->component_key};
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
    const core::FrozenComponentRecord* record = registry.find(component_key);
    ASSERT(record);
    const core::FrozenModuleRecord* module = registry.find_module(record->module_id);
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
    const core::FrozenComponentRecord* record = registry.find(component_key);
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

Result<void> deserialize_scene_json(SceneManager& scenes, const nlohmann::json& scene_json) {
  Result<void> validated = validate_envelope(scene_json);
  REQUIRED_OR_RETURN(validated);
  const std::string name = scene_json["scene"]["name"].get<std::string>();
  Result<std::vector<EntityRecord>> records = parse_entity_records(scene_json);
  REQUIRED_OR_RETURN(records);

  Scene& scene = scenes.create_scene(name);
  for (const EntityRecord& record : *records) {
    const flecs::entity entity = scene.create_entity(record.guid, record.name);
    for (const auto& [key, payload] : record.components.items()) {
      const ComponentCodec* codec = find_component_codec(key);
      Result<void> component = codec->deserialize(entity, payload);
      REQUIRED_OR_RETURN(component);
    }
  }
  derive_local_to_world(scene);
  scenes.set_active_scene(scene.id());
  return {};
}

Result<void> save_scene_file(const Scene& scene, const SceneSerializationContext& serialization,
                             const std::filesystem::path& path) {
  Result<nlohmann::ordered_json> scene_json = serialize_scene_to_json(scene, serialization);
  REQUIRED_OR_RETURN(scene_json);
  return write_text_file(path, (*scene_json).dump(2) + "\n");
}

Result<SceneLoadResult> load_scene_file(SceneManager& scenes, const std::filesystem::path& path) {
  Result<json> scene_json = parse_json_file(path);
  REQUIRED_OR_RETURN(scene_json);
  Result<void> loaded = deserialize_scene_json(scenes, *scene_json);
  REQUIRED_OR_RETURN(loaded);
  Scene* scene = scenes.active_scene();
  return SceneLoadResult{.scene_id = scene->id(), .scene = scene};
}

Result<void> validate_scene_file(const std::filesystem::path& path) {
  Result<json> scene_json = parse_json_file(path);
  REQUIRED_OR_RETURN(scene_json);
  return validate_envelope(*scene_json);
}
namespace {

using DiagnosticPath = core::DiagnosticPath;
using FieldRecord = core::ComponentFieldRegistration;

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
  using core::ComponentFieldKind;
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
      if (!value.is_string()) {
        add_validation_error(report, std::move(path), std::string{label} + " must be an enum key");
        return;
      }
      const std::string key = value.get<std::string>();
      const bool known_value =
          field.enumeration &&
          std::ranges::any_of(field.enumeration->values,
                              [&key](const auto& enum_value) { return enum_value.key == key; });
      if (!known_value) {
        add_validation_error(report, std::move(path),
                             std::string{label} + " must be a known enum key");
      }
      return;
    }
  }
}

[[nodiscard]] const FieldRecord* find_field(const core::FrozenComponentRecord& component,
                                            std::string_view key) {
  const auto it = std::ranges::find(component.fields, key, &FieldRecord::key);
  return it == component.fields.end() ? nullptr : &*it;
}

void validate_component_payload(core::DiagnosticReport& report,
                                const SceneSerializationContext& serialization,
                                const core::FrozenComponentRecord& component, const json& payload,
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

  if (!serialization.find_binding(component.component_key)) {
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
                             const core::ComponentRegistry& registry, const json& required_modules,
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
    const core::FrozenModuleRecord* record = registry.find_module(module_id);
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
    const core::FrozenComponentRecord* record = registry.find(component_key);
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

Result<void, core::DiagnosticReport> validate_scene_file(
    const SceneSerializationContext& serialization, const nlohmann::json& scene_json) {
  core::DiagnosticReport report;
  const core::ComponentRegistry& registry = serialization.component_registry();

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
        const core::FrozenComponentRecord* component = registry.find(component_key);
        if (!component) {
          add_validation_error(report, component_path,
                               "entity component '" + component_key + "' is not registered");
          continue;
        }
        if (component->storage != core::ComponentStoragePolicy::Authored) {
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

Result<std::vector<std::byte>> cook_scene_to_memory(const nlohmann::json& json) {
  (void)json;
  return make_unexpected(
      "cooked scenes are not supported while JSON v2 serialization is being rebuilt");
}

Result<void> cook_scene_file(const std::filesystem::path& input_path,
                             const std::filesystem::path& output_path) {
  (void)input_path;
  (void)output_path;
  return make_unexpected(
      "cooked scenes are not supported while JSON v2 serialization is being rebuilt");
}

Result<nlohmann::json> dump_cooked_scene_to_json(std::span<const std::byte> bytes) {
  (void)bytes;
  return make_unexpected(
      "cooked scenes are not supported while JSON v2 serialization is being rebuilt");
}

Result<void> dump_cooked_scene_file(const std::filesystem::path& input_path,
                                    const std::filesystem::path& output_path) {
  (void)input_path;
  (void)output_path;
  return make_unexpected(
      "cooked scenes are not supported while JSON v2 serialization is being rebuilt");
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
