#include "engine/scene/SceneSerialization.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <flecs/addons/cpp/entity.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <nlohmann/json.hpp>

#include "core/Result.hpp"
#include "engine/scene/SceneComponents.hpp"

namespace teng::engine {
namespace {

using json = nlohmann::json;

constexpr uint64_t k_max_json_safe_integer = 9007199254740991ull;
constexpr std::array<char, 8> k_cooked_magic{'T', 'S', 'C', 'N', 'C', 'O', 'O', 'K'};
constexpr uint32_t k_cooked_header_size = 88;

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

[[nodiscard]] Result<std::vector<std::byte>> read_binary_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return make_unexpected(io_error(path, "open"));
  }
  std::vector<char> chars{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
  std::vector<std::byte> bytes(chars.size());
  std::memcpy(bytes.data(), chars.data(), chars.size());
  return bytes;
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

[[nodiscard]] Result<void> write_binary_file(const std::filesystem::path& path,
                                             std::span<const std::byte> bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return make_unexpected(io_error(path, "write"));
  }
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
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

[[nodiscard]] Result<std::array<float, 3>> required_vec3(const json& object,
                                                         std::string_view field,
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

[[nodiscard]] Result<std::array<float, 4>> required_vec4(const json& object,
                                                         std::string_view field,
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
  Result<std::array<float, 3>> direction =
      required_vec3(payload, "direction", "directional_light");
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
    records.push_back(EntityRecord{.guid = *guid, .name = std::move(name), .components = *components_it});
  }

  std::ranges::sort(records, [](const EntityRecord& a, const EntityRecord& b) {
    return a.guid < b.guid;
  });
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

void write_u32(std::vector<std::byte>& out, uint32_t value) {
  for (uint32_t shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
  }
}

void write_i32(std::vector<std::byte>& out, int value) {
  write_u32(out, static_cast<uint32_t>(value));
}

void write_u64(std::vector<std::byte>& out, uint64_t value) {
  for (uint32_t shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
  }
}

void write_float(std::vector<std::byte>& out, float value) {
  write_u32(out, std::bit_cast<uint32_t>(value));
}

void write_asset_id(std::vector<std::byte>& out, AssetId id) {
  write_u64(out, id.high);
  write_u64(out, id.low);
}

[[nodiscard]] Result<uint32_t> read_u32(std::span<const std::byte> bytes, size_t& offset) {
  if (offset + 4 > bytes.size()) {
    return make_unexpected("cooked scene truncated while reading u32");
  }
  uint32_t value{};
  for (uint32_t shift = 0; shift < 32; shift += 8) {
    value |= static_cast<uint32_t>(bytes[offset++]) << shift;
  }
  return value;
}

[[nodiscard]] Result<int> read_i32(std::span<const std::byte> bytes, size_t& offset) {
  Result<uint32_t> value = read_u32(bytes, offset);
  REQUIRED_OR_RETURN(value);
  return static_cast<int>(*value);
}

[[nodiscard]] Result<uint64_t> read_u64(std::span<const std::byte> bytes, size_t& offset) {
  if (offset + 8 > bytes.size()) {
    return make_unexpected("cooked scene truncated while reading u64");
  }
  uint64_t value{};
  for (uint32_t shift = 0; shift < 64; shift += 8) {
    value |= static_cast<uint64_t>(bytes[offset++]) << shift;
  }
  return value;
}

[[nodiscard]] Result<float> read_float(std::span<const std::byte> bytes, size_t& offset) {
  Result<uint32_t> value = read_u32(bytes, offset);
  REQUIRED_OR_RETURN(value);
  return std::bit_cast<float>(*value);
}

[[nodiscard]] Result<AssetId> read_asset_id(std::span<const std::byte> bytes, size_t& offset) {
  Result<uint64_t> high = read_u64(bytes, offset);
  REQUIRED_OR_RETURN(high);
  Result<uint64_t> low = read_u64(bytes, offset);
  REQUIRED_OR_RETURN(low);
  return AssetId::from_parts(*high, *low);
}

void write_transform_blob(std::vector<std::byte>& out, const json& payload) {
  const Transform transform = *parse_transform(payload);
  write_float(out, transform.translation.x);
  write_float(out, transform.translation.y);
  write_float(out, transform.translation.z);
  write_float(out, transform.rotation.w);
  write_float(out, transform.rotation.x);
  write_float(out, transform.rotation.y);
  write_float(out, transform.rotation.z);
  write_float(out, transform.scale.x);
  write_float(out, transform.scale.y);
  write_float(out, transform.scale.z);
}

[[nodiscard]] Result<json> read_transform_blob(std::span<const std::byte> bytes) {
  size_t offset = 0;
  Transform transform;
  Result<float> tx = read_float(bytes, offset);
  REQUIRED_OR_RETURN(tx);
  Result<float> ty = read_float(bytes, offset);
  REQUIRED_OR_RETURN(ty);
  Result<float> tz = read_float(bytes, offset);
  REQUIRED_OR_RETURN(tz);
  Result<float> rw = read_float(bytes, offset);
  REQUIRED_OR_RETURN(rw);
  Result<float> rx = read_float(bytes, offset);
  REQUIRED_OR_RETURN(rx);
  Result<float> ry = read_float(bytes, offset);
  REQUIRED_OR_RETURN(ry);
  Result<float> rz = read_float(bytes, offset);
  REQUIRED_OR_RETURN(rz);
  Result<float> sx = read_float(bytes, offset);
  REQUIRED_OR_RETURN(sx);
  Result<float> sy = read_float(bytes, offset);
  REQUIRED_OR_RETURN(sy);
  Result<float> sz = read_float(bytes, offset);
  REQUIRED_OR_RETURN(sz);
  transform.translation = {*tx, *ty, *tz};
  transform.rotation = {*rw, *rx, *ry, *rz};
  transform.scale = {*sx, *sy, *sz};
  return transform_json(transform);
}

void write_component_blob(std::vector<std::byte>& out, std::string_view key, const json& payload) {
  if (key == "transform") {
    write_transform_blob(out, payload);
  } else if (key == "camera") {
    const Camera camera = *parse_camera(payload);
    write_float(out, camera.fov_y);
    write_float(out, camera.z_near);
    write_float(out, camera.z_far);
    write_u32(out, camera.primary ? 1u : 0u);
  } else if (key == "directional_light") {
    const DirectionalLight light = *parse_directional_light(payload);
    write_float(out, light.direction.x);
    write_float(out, light.direction.y);
    write_float(out, light.direction.z);
    write_float(out, light.color.x);
    write_float(out, light.color.y);
    write_float(out, light.color.z);
    write_float(out, light.intensity);
  } else if (key == "mesh_renderable") {
    const MeshRenderable mesh = *parse_mesh_renderable(payload);
    write_asset_id(out, mesh.model);
  } else if (key == "sprite_renderable") {
    const SpriteRenderable sprite = *parse_sprite_renderable(payload);
    write_asset_id(out, sprite.texture);
    write_float(out, sprite.tint.x);
    write_float(out, sprite.tint.y);
    write_float(out, sprite.tint.z);
    write_float(out, sprite.tint.w);
    write_i32(out, sprite.sorting_layer);
    write_i32(out, sprite.sorting_order);
  }
}

[[nodiscard]] Result<json> read_component_blob(std::string_view key,
                                               std::span<const std::byte> bytes) {
  if (key == "transform") {
    return read_transform_blob(bytes);
  }
  size_t offset = 0;
  if (key == "camera") {
    Result<float> fov_y = read_float(bytes, offset);
    REQUIRED_OR_RETURN(fov_y);
    Result<float> z_near = read_float(bytes, offset);
    REQUIRED_OR_RETURN(z_near);
    Result<float> z_far = read_float(bytes, offset);
    REQUIRED_OR_RETURN(z_far);
    Result<uint32_t> primary = read_u32(bytes, offset);
    REQUIRED_OR_RETURN(primary);
    return camera_json({.fov_y = *fov_y, .z_near = *z_near, .z_far = *z_far, .primary = *primary != 0});
  }
  if (key == "directional_light") {
    DirectionalLight light;
    Result<float> dx = read_float(bytes, offset);
    REQUIRED_OR_RETURN(dx);
    Result<float> dy = read_float(bytes, offset);
    REQUIRED_OR_RETURN(dy);
    Result<float> dz = read_float(bytes, offset);
    REQUIRED_OR_RETURN(dz);
    Result<float> cx = read_float(bytes, offset);
    REQUIRED_OR_RETURN(cx);
    Result<float> cy = read_float(bytes, offset);
    REQUIRED_OR_RETURN(cy);
    Result<float> cz = read_float(bytes, offset);
    REQUIRED_OR_RETURN(cz);
    Result<float> intensity = read_float(bytes, offset);
    REQUIRED_OR_RETURN(intensity);
    light.direction = {*dx, *dy, *dz};
    light.color = {*cx, *cy, *cz};
    light.intensity = *intensity;
    return directional_light_json(light);
  }
  if (key == "mesh_renderable") {
    Result<AssetId> model = read_asset_id(bytes, offset);
    REQUIRED_OR_RETURN(model);
    return mesh_renderable_json({.model = *model});
  }
  if (key == "sprite_renderable") {
    Result<AssetId> texture = read_asset_id(bytes, offset);
    REQUIRED_OR_RETURN(texture);
    Result<float> r = read_float(bytes, offset);
    REQUIRED_OR_RETURN(r);
    Result<float> g = read_float(bytes, offset);
    REQUIRED_OR_RETURN(g);
    Result<float> b = read_float(bytes, offset);
    REQUIRED_OR_RETURN(b);
    Result<float> a = read_float(bytes, offset);
    REQUIRED_OR_RETURN(a);
    Result<int> sorting_layer = read_i32(bytes, offset);
    REQUIRED_OR_RETURN(sorting_layer);
    Result<int> sorting_order = read_i32(bytes, offset);
    REQUIRED_OR_RETURN(sorting_order);
    return sprite_renderable_json({.texture = *texture,
                                   .tint = {*r, *g, *b, *a},
                                   .sorting_layer = *sorting_layer,
                                   .sorting_order = *sorting_order});
  }
  return make_unexpected("cooked scene has unknown component key '" + std::string(key) + "'");
}

[[nodiscard]] uint32_t component_bit(std::string_view key) {
  const ComponentCodec* codec = find_component_codec(key);
  return codec ? codec->bit : 0;
}

[[nodiscard]] uint32_t intern_string(std::vector<std::string>& strings,
                                     std::unordered_map<std::string, uint32_t>& indices,
                                     std::string text) {
  if (const auto it = indices.find(text); it != indices.end()) {
    return it->second;
  }
  const auto index = static_cast<uint32_t>(strings.size());
  indices.emplace(text, index);
  strings.push_back(std::move(text));
  return index;
}

void append_section(std::vector<std::byte>& out, const std::vector<std::byte>& section) {
  out.insert(out.end(), section.begin(), section.end());
}

}  // namespace

Result<nlohmann::json> serialize_scene_to_json(const Scene& scene) {
  json root{{"registry_version", k_scene_registry_version},
            {"scene", json{{"name", scene.name()}}},
            {"entities", json::array()}};

  struct SerializedEntity {
    EntityGuid guid;
    json value;
  };
  std::vector<SerializedEntity> entities;

  scene.world().each([&entities](flecs::entity entity, const EntityGuidComponent& guid_component) {
    json components = json::object();
    for (const ComponentCodec& codec : component_codecs()) {
      codec.serialize(entity, components);
    }

    json entity_json{{"components", std::move(components)}, {"guid", guid_component.guid.value}};
    if (const auto* name = entity.try_get<Name>(); name && !name->value.empty()) {
      entity_json["name"] = name->value;
    }
    entities.push_back(SerializedEntity{.guid = guid_component.guid, .value = std::move(entity_json)});
  });

  std::ranges::sort(entities, [](const SerializedEntity& a, const SerializedEntity& b) {
    return a.guid < b.guid;
  });
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

Result<void> save_scene_file(const Scene& scene, const std::filesystem::path& path) {
  Result<json> scene_json = serialize_scene_to_json(scene);
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

Result<std::vector<std::byte>> cook_scene_to_memory(const nlohmann::json& scene_json) {
  if (std::endian::native != std::endian::little) {
    return make_unexpected("cooked scenes are little-endian only");
  }
  Result<void> validated = validate_envelope(scene_json);
  REQUIRED_OR_RETURN(validated);
  Result<std::vector<EntityRecord>> records = parse_entity_records(scene_json);
  REQUIRED_OR_RETURN(records);

  std::vector<std::string> strings;
  std::unordered_map<std::string, uint32_t> string_indices;
  const uint32_t scene_name_index =
      intern_string(strings, string_indices, scene_json["scene"]["name"].get<std::string>());
  std::vector<CookEntity> entities;
  std::vector<CookComponent> components;
  std::vector<std::byte> blobs;
  std::vector<AssetId> assets;
  std::unordered_set<AssetId> seen_assets;

  for (size_t entity_index = 0; entity_index < (*records).size(); ++entity_index) {
    const EntityRecord& record = (*records)[entity_index];
    CookEntity cooked_entity{.guid = record.guid,
                             .name_index = intern_string(strings, string_indices, record.name),
                             .component_mask = 0};
    for (const auto& [key, payload] : record.components.items()) {
      const uint32_t key_index = intern_string(strings, string_indices, key);
      const uint64_t offset = blobs.size();
      write_component_blob(blobs, key, payload);
      cooked_entity.component_mask |= component_bit(key);
      components.push_back(CookComponent{.entity_index = static_cast<uint32_t>(entity_index),
                                         .key_index = key_index,
                                         .blob_offset = offset,
                                         .blob_size = blobs.size() - offset});
      if (key == "mesh_renderable") {
        const MeshRenderable mesh = *parse_mesh_renderable(payload);
        if (!seen_assets.contains(mesh.model)) {
          seen_assets.insert(mesh.model);
          assets.push_back(mesh.model);
        }
      } else if (key == "sprite_renderable") {
        const SpriteRenderable sprite = *parse_sprite_renderable(payload);
        if (!seen_assets.contains(sprite.texture)) {
          seen_assets.insert(sprite.texture);
          assets.push_back(sprite.texture);
        }
      }
    }
    entities.push_back(cooked_entity);
  }
  std::ranges::sort(assets, [](AssetId a, AssetId b) { return a < b; });

  std::vector<std::byte> string_section;
  for (const std::string& text : strings) {
    write_u32(string_section, static_cast<uint32_t>(text.size()));
    string_section.insert(string_section.end(), reinterpret_cast<const std::byte*>(text.data()),
                          reinterpret_cast<const std::byte*>(text.data() + text.size()));
  }

  std::vector<std::byte> asset_section;
  for (const AssetId asset : assets) {
    write_asset_id(asset_section, asset);
  }

  std::vector<std::byte> entity_section;
  for (const CookEntity& entity : entities) {
    write_u64(entity_section, entity.guid.value);
    write_u32(entity_section, entity.name_index);
    write_u32(entity_section, entity.component_mask);
  }

  std::vector<std::byte> component_section;
  for (const CookComponent& component : components) {
    write_u32(component_section, component.entity_index);
    write_u32(component_section, component.key_index);
    write_u64(component_section, component.blob_offset);
    write_u64(component_section, component.blob_size);
  }

  const uint64_t string_offset = k_cooked_header_size;
  const uint64_t asset_offset = string_offset + string_section.size();
  const uint64_t entity_offset = asset_offset + asset_section.size();
  const uint64_t component_offset = entity_offset + entity_section.size();
  const uint64_t blob_offset = component_offset + component_section.size();

  std::vector<std::byte> out;
  out.reserve(static_cast<size_t>(blob_offset) + blobs.size());
  for (const char c : k_cooked_magic) {
    out.push_back(static_cast<std::byte>(c));
  }
  write_u32(out, k_scene_binary_format_version);
  write_u32(out, k_scene_registry_version);
  write_u32(out, 0);
  write_u32(out, scene_name_index);
  write_u64(out, string_offset);
  write_u32(out, static_cast<uint32_t>(strings.size()));
  write_u64(out, asset_offset);
  write_u32(out, static_cast<uint32_t>(assets.size()));
  write_u64(out, entity_offset);
  write_u32(out, static_cast<uint32_t>(entities.size()));
  write_u64(out, component_offset);
  write_u32(out, static_cast<uint32_t>(components.size()));
  write_u64(out, blob_offset);
  write_u64(out, static_cast<uint64_t>(blobs.size()));
  append_section(out, string_section);
  append_section(out, asset_section);
  append_section(out, entity_section);
  append_section(out, component_section);
  append_section(out, blobs);
  return out;
}

Result<void> cook_scene_file(const std::filesystem::path& input_path,
                             const std::filesystem::path& output_path) {
  Result<json> scene_json = parse_json_file(input_path);
  REQUIRED_OR_RETURN(scene_json);
  Result<std::vector<std::byte>> bytes = cook_scene_to_memory(*scene_json);
  REQUIRED_OR_RETURN(bytes);
  return write_binary_file(output_path, *bytes);
}

Result<nlohmann::json> dump_cooked_scene_to_json(std::span<const std::byte> bytes) {
  if (bytes.size() < k_cooked_header_size) {
    return make_unexpected("cooked scene is smaller than the header");
  }
  for (size_t i = 0; i < k_cooked_magic.size(); ++i) {
    if (static_cast<char>(bytes[i]) != k_cooked_magic[i]) {
      return make_unexpected("cooked scene has invalid magic");
    }
  }
  size_t offset = k_cooked_magic.size();
  Result<uint32_t> binary_version = read_u32(bytes, offset);
  REQUIRED_OR_RETURN(binary_version);
  Result<uint32_t> registry_version = read_u32(bytes, offset);
  REQUIRED_OR_RETURN(registry_version);
  Result<uint32_t> flags = read_u32(bytes, offset);
  REQUIRED_OR_RETURN(flags);
  (void)flags;
  Result<uint32_t> scene_name_index = read_u32(bytes, offset);
  REQUIRED_OR_RETURN(scene_name_index);
  Result<uint64_t> string_offset = read_u64(bytes, offset);
  REQUIRED_OR_RETURN(string_offset);
  Result<uint32_t> string_count = read_u32(bytes, offset);
  REQUIRED_OR_RETURN(string_count);
  Result<uint64_t> asset_offset = read_u64(bytes, offset);
  REQUIRED_OR_RETURN(asset_offset);
  Result<uint32_t> asset_count = read_u32(bytes, offset);
  REQUIRED_OR_RETURN(asset_count);
  Result<uint64_t> entity_offset = read_u64(bytes, offset);
  REQUIRED_OR_RETURN(entity_offset);
  Result<uint32_t> entity_count = read_u32(bytes, offset);
  REQUIRED_OR_RETURN(entity_count);
  Result<uint64_t> component_offset = read_u64(bytes, offset);
  REQUIRED_OR_RETURN(component_offset);
  Result<uint32_t> component_count = read_u32(bytes, offset);
  REQUIRED_OR_RETURN(component_count);
  Result<uint64_t> blob_offset = read_u64(bytes, offset);
  REQUIRED_OR_RETURN(blob_offset);
  Result<uint64_t> blob_size = read_u64(bytes, offset);
  REQUIRED_OR_RETURN(blob_size);

  if (*binary_version != k_scene_binary_format_version ||
      *registry_version != k_scene_registry_version) {
    return make_unexpected("cooked scene version pair is not supported");
  }
  if (*blob_offset + *blob_size > bytes.size()) {
    return make_unexpected("cooked scene blob section is out of bounds");
  }

  std::vector<std::string> strings;
  auto string_read = static_cast<size_t>(*string_offset);
  for (uint32_t i = 0; i < *string_count; ++i) {
    Result<uint32_t> size = read_u32(bytes, string_read);
    REQUIRED_OR_RETURN(size);
    if (string_read + *size > bytes.size()) {
      return make_unexpected("cooked scene string table is out of bounds");
    }
    strings.emplace_back(reinterpret_cast<const char*>(bytes.data() + string_read), *size);
    string_read += *size;
  }
  if (*scene_name_index >= strings.size()) {
    return make_unexpected("cooked scene name string index is out of range");
  }

  auto asset_read = static_cast<size_t>(*asset_offset);
  for (uint32_t i = 0; i < *asset_count; ++i) {
    Result<AssetId> asset = read_asset_id(bytes, asset_read);
    REQUIRED_OR_RETURN(asset);
  }

  struct DumpEntity {
    EntityGuid guid;
    uint32_t name_index;
    uint32_t component_mask;
    json components = json::object();
  };
  std::vector<DumpEntity> entities;
  auto entity_read = static_cast<size_t>(*entity_offset);
  for (uint32_t i = 0; i < *entity_count; ++i) {
    Result<uint64_t> guid = read_u64(bytes, entity_read);
    REQUIRED_OR_RETURN(guid);
    Result<uint32_t> name_index = read_u32(bytes, entity_read);
    REQUIRED_OR_RETURN(name_index);
    Result<uint32_t> component_mask = read_u32(bytes, entity_read);
    REQUIRED_OR_RETURN(component_mask);
    if (*name_index >= strings.size()) {
      return make_unexpected("cooked scene entity name index is out of range");
    }
    entities.push_back(DumpEntity{.guid = EntityGuid{*guid},
                                  .name_index = *name_index,
                                  .component_mask = *component_mask});
  }

  auto component_read = static_cast<size_t>(*component_offset);
  for (uint32_t i = 0; i < *component_count; ++i) {
    Result<uint32_t> entity_index = read_u32(bytes, component_read);
    REQUIRED_OR_RETURN(entity_index);
    Result<uint32_t> key_index = read_u32(bytes, component_read);
    REQUIRED_OR_RETURN(key_index);
    Result<uint64_t> component_blob_offset = read_u64(bytes, component_read);
    REQUIRED_OR_RETURN(component_blob_offset);
    Result<uint64_t> component_blob_size = read_u64(bytes, component_read);
    REQUIRED_OR_RETURN(component_blob_size);
    if (*entity_index >= entities.size() || *key_index >= strings.size() ||
        *component_blob_offset + *component_blob_size > *blob_size) {
      return make_unexpected("cooked scene component record is out of bounds");
    }
    const std::string& key = strings[*key_index];
    const auto payload_offset = static_cast<size_t>(*blob_offset + *component_blob_offset);
    Result<json> payload = read_component_blob(
        key, bytes.subspan(payload_offset, static_cast<size_t>(*component_blob_size)));
    REQUIRED_OR_RETURN(payload);
    entities[*entity_index].components[key] = *payload;
    if ((entities[*entity_index].component_mask & component_bit(key)) == 0) {
      return make_unexpected("cooked scene component mask does not match component records");
    }
  }

  json root{{"registry_version", *registry_version},
            {"scene", json{{"name", strings[*scene_name_index]}}},
            {"entities", json::array()}};
  std::ranges::sort(entities, [](const DumpEntity& a, const DumpEntity& b) {
    return a.guid < b.guid;
  });
  for (const DumpEntity& entity : entities) {
    json entity_json{{"components", entity.components}, {"guid", entity.guid.value}};
    if (!strings[entity.name_index].empty()) {
      entity_json["name"] = strings[entity.name_index];
    }
    root["entities"].push_back(std::move(entity_json));
  }
  Result<void> validated = validate_envelope(root);
  REQUIRED_OR_RETURN(validated);
  return root;
}

Result<void> dump_cooked_scene_file(const std::filesystem::path& input_path,
                                    const std::filesystem::path& output_path) {
  Result<std::vector<std::byte>> bytes = read_binary_file(input_path);
  REQUIRED_OR_RETURN(bytes);
  Result<json> scene_json = dump_cooked_scene_to_json(*bytes);
  REQUIRED_OR_RETURN(scene_json);
  return write_text_file(output_path, (*scene_json).dump(2) + "\n");
}

Result<void> migrate_scene_file(const std::filesystem::path& input_path,
                                const std::filesystem::path& output_path) {
  Result<json> scene_json = parse_json_file(input_path);
  REQUIRED_OR_RETURN(scene_json);
  Result<void> validated = validate_envelope(*scene_json);
  REQUIRED_OR_RETURN(validated);
  return write_text_file(output_path, (*scene_json).dump(2) + "\n");
}

}  // namespace teng::engine
