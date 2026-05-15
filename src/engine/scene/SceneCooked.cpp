#include "engine/scene/SceneCooked.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/EAssert.hpp"
#include "engine/content/BinaryReader.hpp"
#include "engine/content/BinaryWriter.hpp"
#include "engine/content/CookedArtifact.hpp"
#include "engine/scene/ComponentRegistry.hpp"
#include "engine/scene/SceneSerialization.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace teng::engine {
namespace {

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

constexpr std::string_view k_magic = "TSCNCOOK";
constexpr std::string_view k_artifact_kind = "scene";
constexpr uint32_t k_section_strings = 1;
constexpr uint32_t k_section_schema = 2;
constexpr uint32_t k_section_scene = 3;
constexpr uint32_t k_section_entities = 4;
constexpr uint32_t k_section_components = 5;
constexpr uint32_t k_section_payloads = 6;
constexpr uint32_t k_section_count = 6;
constexpr uint32_t k_invalid_string_index = std::numeric_limits<uint32_t>::max();

struct EntityCookRecord {
  EntityGuid guid;
  uint32_t name_index{k_invalid_string_index};
  uint32_t component_begin{};
  uint32_t component_count{};
};

struct ComponentCookRecord {
  uint64_t stable_id{};
  uint32_t schema_version{};
  uint64_t payload_offset{};
  uint64_t payload_size{};
  uint32_t component_key_index{};
};

struct ModuleUse {
  std::string id;
  uint32_t version{};
  uint32_t string_index{};
};

struct ComponentUse {
  std::string key;
  std::string module_id;
  uint64_t stable_id{};
  uint32_t schema_version{};
  uint32_t key_string_index{};
  uint32_t module_string_index{};
};

struct DecodedCookedScene {
  std::vector<std::string> strings;
  std::vector<ModuleUse> modules;
  std::vector<ComponentUse> components;
  std::string scene_name;
  std::vector<EntityCookRecord> entities;
  std::vector<ComponentCookRecord> component_records;
  std::span<const std::byte> payloads;
};

class StringTableBuilder {
 public:
  [[nodiscard]] uint32_t add(std::string_view text) {
    std::string key{text};
    if (const auto it = index_by_string_.find(key); it != index_by_string_.end()) {
      return it->second;
    }
    const size_t next = strings_.size();
    ASSERT(next <= std::numeric_limits<uint32_t>::max());
    strings_.push_back(key);
    const auto index = static_cast<uint32_t>(next);
    index_by_string_.emplace(std::move(key), index);
    return index;
  }

  [[nodiscard]] const std::vector<std::string>& strings() const { return strings_; }

 private:
  std::vector<std::string> strings_;
  std::unordered_map<std::string, uint32_t> index_by_string_;
};

[[nodiscard]] std::string io_error(const std::filesystem::path& path, std::string_view action) {
  return "failed to " + std::string(action) + " " + path.string();
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

[[nodiscard]] Result<std::string> read_text_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return make_unexpected(io_error(path, "open"));
  }
  return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] Result<void> write_binary_file(const std::filesystem::path& path,
                                             std::span<const std::byte> bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return make_unexpected(io_error(path, "write"));
  }
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return {};
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

[[nodiscard]] Result<EntityGuid> parse_entity_guid(std::string_view text) {
  if (text.size() != 16) {
    return make_unexpected("entity guid must be a 16-character lowercase hex string");
  }
  uint64_t value{};
  for (const char c : text) {
    value <<= 4u;
    if (c >= '0' && c <= '9') {
      value |= static_cast<uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      value |= static_cast<uint64_t>(c - 'a' + 10);
    } else {
      return make_unexpected("entity guid must be a 16-character lowercase hex string");
    }
  }
  if (value == 0) {
    return make_unexpected("entity guid must be non-zero");
  }
  return EntityGuid{value};
}

[[nodiscard]] std::string entity_guid_lower_hex(EntityGuid guid) {
  std::array<char, 17> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%016llx",
                static_cast<unsigned long long>(guid.value));
  return buffer.data();
}

[[nodiscard]] Result<const std::string*> string_at(const std::vector<std::string>& strings,
                                                   uint32_t index, std::string_view label) {
  if (index >= strings.size()) {
    return make_unexpected(std::string(label) + " references an invalid string index");
  }
  return &strings[index];
}

void write_section(content::BinaryWriter& writer, uint32_t id, std::span<const std::byte> bytes,
                   std::vector<content::CookedSectionDesc>& sections) {
  writer.align_to(8);
  sections.push_back(content::CookedSectionDesc{
      .id = id,
      .offset = static_cast<uint64_t>(writer.size()),
      .size = static_cast<uint64_t>(bytes.size()),
  });
  writer.write_bytes(bytes);
}

void write_string_table(content::BinaryWriter& writer, const std::vector<std::string>& strings) {
  writer.write_u32(static_cast<uint32_t>(strings.size()));
  for (const std::string& text : strings) {
    writer.write_u32(static_cast<uint32_t>(text.size()));
    const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
    writer.write_bytes(std::span<const std::byte>{bytes, text.size()});
  }
}

[[nodiscard]] Result<std::vector<std::string>> read_string_table(std::span<const std::byte> bytes) {
  content::BinaryReader reader(bytes);
  Result<uint32_t> count = reader.read_u32();
  REQUIRED_OR_RETURN(count);
  std::vector<std::string> strings;
  strings.reserve(*count);
  for (uint32_t i = 0; i < *count; ++i) {
    Result<uint32_t> size = reader.read_u32();
    REQUIRED_OR_RETURN(size);
    Result<std::span<const std::byte>> string_bytes = reader.read_bytes(*size);
    REQUIRED_OR_RETURN(string_bytes);
    strings.emplace_back(reinterpret_cast<const char*>(string_bytes->data()), string_bytes->size());
  }
  if (reader.position() != reader.size()) {
    return make_unexpected("string table has trailing bytes");
  }
  return strings;
}

[[nodiscard]] Result<void> encode_field_payload(content::BinaryWriter& writer,
                                                const scene::FrozenComponentFieldRecord& field,
                                                const json& payload) {
  using scene::ComponentFieldKind;
  switch (field.kind) {
    case ComponentFieldKind::Bool:
      writer.write_u8(payload.get<bool>() ? 1 : 0);
      return {};
    case ComponentFieldKind::I32:
      writer.write_i32(payload.get<int32_t>());
      return {};
    case ComponentFieldKind::U32:
      writer.write_u32(payload.get<uint32_t>());
      return {};
    case ComponentFieldKind::F32:
      writer.write_f32(payload.get<float>());
      return {};
    case ComponentFieldKind::String:
      writer.write_u32(payload.get<uint32_t>());
      return {};
    case ComponentFieldKind::Enum: {
      if (!payload.is_number_integer()) {
        return make_unexpected("enum field payload is not an integer");
      }
      const int64_t v = payload.get<int64_t>();
      writer.write_i64(v);
      return {};
    }
    case ComponentFieldKind::Vec2:
    case ComponentFieldKind::Vec3:
    case ComponentFieldKind::Vec4:
    case ComponentFieldKind::Quat:
    case ComponentFieldKind::Mat4:
      for (const json& element : payload) {
        writer.write_f32(element.get<float>());
      }
      return {};
    case ComponentFieldKind::AssetId: {
      const std::optional<AssetId> parsed_asset = AssetId::parse(payload.get<std::string>());
      if (!parsed_asset) {
        return make_unexpected("asset field payload is not a valid AssetId");
      }
      const AssetId asset = *parsed_asset;
      writer.write_u64(asset.high);
      writer.write_u64(asset.low);
      return {};
    }
  }
  return {};
}

[[nodiscard]] Result<json> decode_field_payload(content::BinaryReader& reader,
                                                const scene::FrozenComponentFieldRecord& field,
                                                const std::vector<std::string>& strings) {
  using scene::ComponentFieldKind;
  switch (field.kind) {
    case ComponentFieldKind::Bool: {
      Result<uint8_t> value = reader.read_u8();
      REQUIRED_OR_RETURN(value);
      if (*value > 1) {
        return make_unexpected("boolean field payload is not 0 or 1");
      }
      return json(*value != 0);
    }
    case ComponentFieldKind::I32: {
      Result<int32_t> value = reader.read_i32();
      REQUIRED_OR_RETURN(value);
      return json(*value);
    }
    case ComponentFieldKind::U32: {
      Result<uint32_t> value = reader.read_u32();
      REQUIRED_OR_RETURN(value);
      return json(*value);
    }
    case ComponentFieldKind::F32: {
      Result<float> value = reader.read_f32();
      REQUIRED_OR_RETURN(value);
      return json(*value);
    }
    case ComponentFieldKind::String: {
      Result<uint32_t> index = reader.read_u32();
      REQUIRED_OR_RETURN(index);
      Result<const std::string*> text = string_at(strings, *index, "field payload");
      REQUIRED_OR_RETURN(text);
      return json(**text);
    }
    case ComponentFieldKind::Enum: {
      Result<int64_t> value = reader.read_i64();
      REQUIRED_OR_RETURN(value);
      return json(*value);
    }
    case ComponentFieldKind::Vec2:
    case ComponentFieldKind::Vec3:
    case ComponentFieldKind::Vec4:
    case ComponentFieldKind::Quat:
    case ComponentFieldKind::Mat4: {
      size_t count{};
      if (field.kind == ComponentFieldKind::Vec2) {
        count = 2;
      } else if (field.kind == ComponentFieldKind::Vec3) {
        count = 3;
      } else if (field.kind == ComponentFieldKind::Mat4) {
        count = 16;
      } else {
        count = 4;
      }
      json out = json::array();
      for (size_t i = 0; i < count; ++i) {
        Result<float> value = reader.read_f32();
        REQUIRED_OR_RETURN(value);
        out.push_back(*value);
      }
      return out;
    }
    case ComponentFieldKind::AssetId: {
      Result<uint64_t> high = reader.read_u64();
      REQUIRED_OR_RETURN(high);
      Result<uint64_t> low = reader.read_u64();
      REQUIRED_OR_RETURN(low);
      const AssetId asset = AssetId::from_parts(*high, *low);
      if (!asset.is_valid()) {
        return make_unexpected("asset field payload is not a valid AssetId");
      }
      return json(asset.to_string());
    }
  }
  return make_unexpected("unknown field kind");
}

[[nodiscard]] Result<void> add_field_strings(StringTableBuilder& strings,
                                             const scene::FrozenComponentFieldRecord& field,
                                             const json& value) {
  using scene::ComponentFieldKind;
  switch (field.kind) {
    case ComponentFieldKind::String:
      (void)strings.add(value.get<std::string>());
      return {};
    case ComponentFieldKind::Enum:
    case ComponentFieldKind::Bool:
    case ComponentFieldKind::I32:
    case ComponentFieldKind::U32:
    case ComponentFieldKind::F32:
    case ComponentFieldKind::Vec2:
    case ComponentFieldKind::Vec3:
    case ComponentFieldKind::Vec4:
    case ComponentFieldKind::Quat:
    case ComponentFieldKind::Mat4:
    case ComponentFieldKind::AssetId:
      return {};
  }
  return {};
}

[[nodiscard]] Result<json> prepare_payload_for_encoding(
    StringTableBuilder& strings, const scene::FrozenComponentRecord& component,
    const json& payload) {
  json encoded = json::object();
  for (const scene::FrozenComponentFieldRecord& field : component.fields) {
    const json& value = payload.at(field.key);
    Result<void> added = add_field_strings(strings, field, value);
    REQUIRED_OR_RETURN(added);
    if (field.kind == scene::ComponentFieldKind::String) {
      encoded[field.key] = strings.add(value.get<std::string>());
    } else {
      encoded[field.key] = value;
    }
  }
  return encoded;
}

}  // namespace

Result<std::vector<SceneAssetDependency>> collect_scene_asset_dependencies(
    const SceneSerializationContext& serialization, const nlohmann::json& canonical_scene_json) {
  Result<void, core::DiagnosticReport> validated =
      validate_scene_file_full_report(serialization, canonical_scene_json);
  if (!validated) {
    return make_unexpected(validated.error().to_string());
  }

  std::vector<SceneAssetDependency> dependencies;
  for (const json& entity : canonical_scene_json.at("entities")) {
    Result<EntityGuid> guid = parse_entity_guid(entity.at("guid").get<std::string>());
    REQUIRED_OR_RETURN(guid);
    const json& components = entity.at("components");
    for (const auto& [component_key, payload] : components.items()) {
      const scene::FrozenComponentRecord* component =
          serialization.find_authored_component(component_key);
      ASSERT(component);
      for (const scene::FrozenComponentFieldRecord& field : component->fields) {
        if (field.kind != scene::ComponentFieldKind::AssetId) {
          continue;
        }
        const std::optional<AssetId> parsed_asset =
            AssetId::parse(payload.at(field.key).get<std::string>());
        if (!parsed_asset) {
          return make_unexpected("asset field payload is not a valid AssetId");
        }
        const AssetId asset = *parsed_asset;
        dependencies.push_back(SceneAssetDependency{
            .asset = asset,
            .kind = assets::AssetDependencyKind::Strong,
            .entity = *guid,
            .component_key = component->component_key,
            .field_key = field.key,
        });
      }
    }
  }
  return dependencies;
}

Result<std::vector<std::byte>> cook_scene_to_memory(const SceneSerializationContext& serialization,
                                                    const nlohmann::json& scene_json) {
  Result<ordered_json> canonical = canonicalize_scene_json(serialization, scene_json);
  REQUIRED_OR_RETURN(canonical);
  const json canonical_json = json::parse(canonical->dump());
  Result<std::vector<SceneAssetDependency>> dependencies =
      collect_scene_asset_dependencies(serialization, canonical_json);
  REQUIRED_OR_RETURN(dependencies);

  StringTableBuilder strings;
  std::vector<ModuleUse> modules;
  std::vector<ComponentUse> component_uses;
  std::vector<EntityCookRecord> entities;
  std::vector<ComponentCookRecord> component_records;
  content::BinaryWriter payload_writer;

  const uint32_t scene_name_index =
      strings.add(canonical_json.at("scene").at("name").get<std::string>());

  for (const json& module_json : canonical_json.at("schema").at("required_modules")) {
    const std::string id = module_json.at("id").get<std::string>();
    modules.push_back(ModuleUse{
        .id = id,
        .version = module_json.at("version").get<uint32_t>(),
        .string_index = strings.add(id),
    });
  }

  for (const auto& [component_key, version] :
       canonical_json.at("schema").at("required_components").items()) {
    const scene::FrozenComponentRecord* component =
        serialization.find_authored_component(component_key);
    ASSERT(component);
    component_uses.push_back(ComponentUse{
        .key = component_key,
        .module_id = component->module_id,
        .stable_id = component->stable_id,
        .schema_version = version.get<uint32_t>(),
        .key_string_index = strings.add(component_key),
        .module_string_index = strings.add(component->module_id),
    });
  }

  for (const json& entity_json : canonical_json.at("entities")) {
    Result<EntityGuid> guid = parse_entity_guid(entity_json.at("guid").get<std::string>());
    REQUIRED_OR_RETURN(guid);
    uint32_t name_index = k_invalid_string_index;
    if (const auto name_it = entity_json.find("name"); name_it != entity_json.end()) {
      name_index = strings.add(name_it->get<std::string>());
    }

    const auto component_begin = static_cast<uint32_t>(component_records.size());
    const json& components = entity_json.at("components");
    for (const auto& [component_key, payload] : components.items()) {
      const scene::FrozenComponentRecord* component =
          serialization.find_authored_component(component_key);
      ASSERT(component);
      Result<json> encoded_payload = prepare_payload_for_encoding(strings, *component, payload);
      REQUIRED_OR_RETURN(encoded_payload);
      const auto payload_offset = static_cast<uint64_t>(payload_writer.size());
      for (const scene::FrozenComponentFieldRecord& field : component->fields) {
        Result<void> encoded =
            encode_field_payload(payload_writer, field, encoded_payload->at(field.key));
        REQUIRED_OR_RETURN(encoded);
      }
      component_records.push_back(ComponentCookRecord{
          .stable_id = component->stable_id,
          .schema_version = component->schema_version,
          .payload_offset = payload_offset,
          .payload_size = static_cast<uint64_t>(payload_writer.size()) - payload_offset,
          .component_key_index = strings.add(component_key),
      });
    }
    entities.push_back(EntityCookRecord{
        .guid = *guid,
        .name_index = name_index,
        .component_begin = component_begin,
        .component_count = static_cast<uint32_t>(component_records.size()) - component_begin,
    });
  }

  content::BinaryWriter string_writer;
  write_string_table(string_writer, strings.strings());

  content::BinaryWriter schema_writer;
  schema_writer.write_u32(static_cast<uint32_t>(modules.size()));
  for (const ModuleUse& module : modules) {
    schema_writer.write_u32(module.string_index);
    schema_writer.write_u32(module.version);
  }
  schema_writer.write_u32(static_cast<uint32_t>(component_uses.size()));
  for (const ComponentUse& component : component_uses) {
    schema_writer.write_u64(component.stable_id);
    schema_writer.write_u32(component.key_string_index);
    schema_writer.write_u32(component.schema_version);
    schema_writer.write_u32(component.module_string_index);
  }

  content::BinaryWriter scene_writer;
  scene_writer.write_u32(scene_name_index);

  content::BinaryWriter entity_writer;
  entity_writer.write_u32(static_cast<uint32_t>(entities.size()));
  for (const EntityCookRecord& entity : entities) {
    entity_writer.write_u64(entity.guid.value);
    entity_writer.write_u32(entity.name_index);
    entity_writer.write_u32(entity.component_begin);
    entity_writer.write_u32(entity.component_count);
  }

  content::BinaryWriter component_writer;
  component_writer.write_u32(static_cast<uint32_t>(component_records.size()));
  for (const ComponentCookRecord& component : component_records) {
    component_writer.write_u64(component.stable_id);
    component_writer.write_u32(component.schema_version);
    component_writer.write_u64(component.payload_offset);
    component_writer.write_u64(component.payload_size);
    component_writer.write_u32(component.component_key_index);
  }

  content::BinaryWriter out;
  out.write_fixed_string(k_magic, content::k_cooked_artifact_magic_size);
  out.write_fixed_string(k_artifact_kind, content::k_cooked_artifact_kind_size);
  out.write_u32(k_cooked_scene_binary_format_version);
  out.write_u32(k_cooked_scene_json_format_version);
  out.write_u32(content::k_cooked_artifact_endian_marker);
  out.write_u32(k_section_count);
  for (uint32_t i = 0; i < content::k_cooked_artifact_header_reserved_u32_count; ++i) {
    out.write_u32(0);
  }

  const size_t section_table_offset = out.size();
  for (uint32_t i = 0; i < k_section_count; ++i) {
    out.write_u32(0);
    out.write_u64(0);
    out.write_u64(0);
  }

  std::vector<content::CookedSectionDesc> sections;
  write_section(out, k_section_strings, string_writer.bytes(), sections);
  write_section(out, k_section_schema, schema_writer.bytes(), sections);
  write_section(out, k_section_scene, scene_writer.bytes(), sections);
  write_section(out, k_section_entities, entity_writer.bytes(), sections);
  write_section(out, k_section_components, component_writer.bytes(), sections);
  write_section(out, k_section_payloads, payload_writer.bytes(), sections);

  size_t patch = section_table_offset;
  for (const content::CookedSectionDesc& section : sections) {
    out.patch_u32(patch, section.id);
    patch += sizeof(uint32_t);
    out.patch_u64(patch, section.offset);
    patch += sizeof(uint64_t);
    out.patch_u64(patch, section.size);
    patch += sizeof(uint64_t);
  }
  return out.bytes();
}

Result<void> cook_scene_file(const SceneSerializationContext& serialization,
                             const std::filesystem::path& input_path,
                             const std::filesystem::path& output_path) {
  Result<json> scene_json = parse_json_file(input_path);
  REQUIRED_OR_RETURN(scene_json);
  Result<std::vector<std::byte>> bytes = cook_scene_to_memory(serialization, *scene_json);
  REQUIRED_OR_RETURN(bytes);
  return write_binary_file(output_path, *bytes);
}

namespace {

[[nodiscard]] Result<DecodedCookedScene> decode_cooked_scene_header(
    const SceneSerializationContext& serialization, std::span<const std::byte> bytes) {
  content::BinaryReader reader(bytes);
  Result<std::string> magic = reader.read_fixed_string(content::k_cooked_artifact_magic_size);
  REQUIRED_OR_RETURN(magic);
  if (*magic != k_magic) {
    return make_unexpected("cooked scene has invalid magic");
  }
  Result<std::string> kind = reader.read_fixed_string(content::k_cooked_artifact_kind_size);
  REQUIRED_OR_RETURN(kind);
  if (*kind != k_artifact_kind) {
    return make_unexpected("cooked artifact kind is not scene");
  }
  Result<uint32_t> binary_version = reader.read_u32();
  REQUIRED_OR_RETURN(binary_version);
  if (*binary_version != k_cooked_scene_binary_format_version) {
    return make_unexpected("unsupported cooked scene binary format version");
  }
  Result<uint32_t> scene_version = reader.read_u32();
  REQUIRED_OR_RETURN(scene_version);
  if (*scene_version != k_cooked_scene_json_format_version) {
    return make_unexpected("unsupported cooked scene JSON format version");
  }
  Result<uint32_t> endian = reader.read_u32();
  REQUIRED_OR_RETURN(endian);
  if (*endian != content::k_cooked_artifact_endian_marker) {
    return make_unexpected("unsupported cooked scene endian marker");
  }
  Result<uint32_t> section_count = reader.read_u32();
  REQUIRED_OR_RETURN(section_count);
  if (*section_count != k_section_count) {
    return make_unexpected("unsupported cooked scene section count");
  }
  Result<void> skipped =
      reader.skip(content::k_cooked_artifact_header_reserved_u32_count * sizeof(uint32_t));
  REQUIRED_OR_RETURN(skipped);

  std::vector<content::CookedSectionDesc> sections;
  sections.reserve(*section_count);
  for (uint32_t i = 0; i < *section_count; ++i) {
    Result<uint32_t> id = reader.read_u32();
    REQUIRED_OR_RETURN(id);
    Result<uint64_t> offset = reader.read_u64();
    REQUIRED_OR_RETURN(offset);
    Result<uint64_t> size = reader.read_u64();
    REQUIRED_OR_RETURN(size);
    sections.push_back(content::CookedSectionDesc{.id = *id, .offset = *offset, .size = *size});
  }
  Result<void> section_validation = content::validate_sections(bytes, sections);
  REQUIRED_OR_RETURN(section_validation);

  const auto read_required_section = [&](uint32_t id) -> Result<std::span<const std::byte>> {
    const content::CookedSectionDesc* section = content::find_section(sections, id);
    if (!section) {
      return make_unexpected("cooked scene is missing a required section");
    }
    return content::checked_subspan(bytes, section->offset, section->size);
  };

  Result<std::span<const std::byte>> string_bytes = read_required_section(k_section_strings);
  REQUIRED_OR_RETURN(string_bytes);
  Result<std::vector<std::string>> strings = read_string_table(*string_bytes);
  REQUIRED_OR_RETURN(strings);

  Result<std::span<const std::byte>> schema_bytes = read_required_section(k_section_schema);
  REQUIRED_OR_RETURN(schema_bytes);
  content::BinaryReader schema_reader(*schema_bytes);
  Result<uint32_t> module_count = schema_reader.read_u32();
  REQUIRED_OR_RETURN(module_count);
  std::vector<ModuleUse> modules;
  modules.reserve(*module_count);
  for (uint32_t i = 0; i < *module_count; ++i) {
    Result<uint32_t> id_index = schema_reader.read_u32();
    REQUIRED_OR_RETURN(id_index);
    Result<uint32_t> version = schema_reader.read_u32();
    REQUIRED_OR_RETURN(version);
    Result<const std::string*> id = string_at(*strings, *id_index, "module metadata");
    REQUIRED_OR_RETURN(id);
    const scene::FrozenModuleRecord* module = serialization.component_registry().find_module(**id);
    if (!module || module->version != *version) {
      return make_unexpected("cooked scene requires unsupported module '" + **id + "'");
    }
    modules.push_back(ModuleUse{.id = **id, .version = *version, .string_index = *id_index});
  }

  Result<uint32_t> component_count = schema_reader.read_u32();
  REQUIRED_OR_RETURN(component_count);
  std::vector<ComponentUse> components;
  components.reserve(*component_count);
  for (uint32_t i = 0; i < *component_count; ++i) {
    Result<uint64_t> stable_id = schema_reader.read_u64();
    REQUIRED_OR_RETURN(stable_id);
    Result<uint32_t> key_index = schema_reader.read_u32();
    REQUIRED_OR_RETURN(key_index);
    Result<uint32_t> schema_version = schema_reader.read_u32();
    REQUIRED_OR_RETURN(schema_version);
    Result<uint32_t> module_index = schema_reader.read_u32();
    REQUIRED_OR_RETURN(module_index);
    Result<const std::string*> key = string_at(*strings, *key_index, "component metadata");
    REQUIRED_OR_RETURN(key);
    Result<const std::string*> module_id = string_at(*strings, *module_index, "component metadata");
    REQUIRED_OR_RETURN(module_id);
    const scene::FrozenComponentRecord* component = serialization.find_authored_component(**key);
    if (!component) {
      return make_unexpected("cooked scene references unregistered authored component '" + **key +
                             "'");
    }
    if (component->stable_id != *stable_id) {
      return make_unexpected("cooked scene component stable ID does not match key '" + **key + "'");
    }
    if (component->schema_version != *schema_version) {
      return make_unexpected("cooked scene component '" + **key +
                             "' has unsupported schema version");
    }
    if (component->module_id != **module_id) {
      return make_unexpected("cooked scene component '" + **key + "' has mismatched module");
    }
    components.push_back(ComponentUse{
        .key = **key,
        .module_id = **module_id,
        .stable_id = *stable_id,
        .schema_version = *schema_version,
        .key_string_index = *key_index,
        .module_string_index = *module_index,
    });
  }
  if (schema_reader.position() != schema_reader.size()) {
    return make_unexpected("schema section has trailing bytes");
  }

  Result<std::span<const std::byte>> scene_bytes = read_required_section(k_section_scene);
  REQUIRED_OR_RETURN(scene_bytes);
  content::BinaryReader scene_reader(*scene_bytes);
  Result<uint32_t> scene_name_index = scene_reader.read_u32();
  REQUIRED_OR_RETURN(scene_name_index);
  Result<const std::string*> scene_name = string_at(*strings, *scene_name_index, "scene metadata");
  REQUIRED_OR_RETURN(scene_name);
  if (scene_reader.position() != scene_reader.size()) {
    return make_unexpected("scene metadata section has trailing bytes");
  }

  Result<std::span<const std::byte>> entity_bytes = read_required_section(k_section_entities);
  REQUIRED_OR_RETURN(entity_bytes);
  content::BinaryReader entity_reader(*entity_bytes);
  Result<uint32_t> entity_count = entity_reader.read_u32();
  REQUIRED_OR_RETURN(entity_count);
  std::vector<EntityCookRecord> entities;
  entities.reserve(*entity_count);
  for (uint32_t i = 0; i < *entity_count; ++i) {
    Result<uint64_t> guid = entity_reader.read_u64();
    REQUIRED_OR_RETURN(guid);
    Result<uint32_t> name_index = entity_reader.read_u32();
    REQUIRED_OR_RETURN(name_index);
    Result<uint32_t> component_begin = entity_reader.read_u32();
    REQUIRED_OR_RETURN(component_begin);
    Result<uint32_t> component_count_for_entity = entity_reader.read_u32();
    REQUIRED_OR_RETURN(component_count_for_entity);
    if (*guid == 0) {
      return make_unexpected("cooked scene contains an invalid entity guid");
    }
    if (*name_index != k_invalid_string_index) {
      Result<const std::string*> ignored = string_at(*strings, *name_index, "entity metadata");
      REQUIRED_OR_RETURN(ignored);
    }
    entities.push_back(EntityCookRecord{
        .guid = EntityGuid{*guid},
        .name_index = *name_index,
        .component_begin = *component_begin,
        .component_count = *component_count_for_entity,
    });
  }
  if (entity_reader.position() != entity_reader.size()) {
    return make_unexpected("entity section has trailing bytes");
  }

  Result<std::span<const std::byte>> component_bytes = read_required_section(k_section_components);
  REQUIRED_OR_RETURN(component_bytes);
  content::BinaryReader component_reader(*component_bytes);
  Result<uint32_t> record_count = component_reader.read_u32();
  REQUIRED_OR_RETURN(record_count);
  std::vector<ComponentCookRecord> component_records;
  component_records.reserve(*record_count);
  for (uint32_t i = 0; i < *record_count; ++i) {
    Result<uint64_t> stable_id = component_reader.read_u64();
    REQUIRED_OR_RETURN(stable_id);
    Result<uint32_t> schema_version = component_reader.read_u32();
    REQUIRED_OR_RETURN(schema_version);
    Result<uint64_t> payload_offset = component_reader.read_u64();
    REQUIRED_OR_RETURN(payload_offset);
    Result<uint64_t> payload_size = component_reader.read_u64();
    REQUIRED_OR_RETURN(payload_size);
    Result<uint32_t> component_key_index = component_reader.read_u32();
    REQUIRED_OR_RETURN(component_key_index);
    Result<const std::string*> key = string_at(*strings, *component_key_index, "component record");
    REQUIRED_OR_RETURN(key);
    const scene::FrozenComponentRecord* component = serialization.find_authored_component(**key);
    if (!component || component->stable_id != *stable_id ||
        component->schema_version != *schema_version) {
      return make_unexpected("cooked scene component record does not match active registry");
    }
    component_records.push_back(ComponentCookRecord{
        .stable_id = *stable_id,
        .schema_version = *schema_version,
        .payload_offset = *payload_offset,
        .payload_size = *payload_size,
        .component_key_index = *component_key_index,
    });
  }
  if (component_reader.position() != component_reader.size()) {
    return make_unexpected("component section has trailing bytes");
  }

  Result<std::span<const std::byte>> payload_bytes = read_required_section(k_section_payloads);
  REQUIRED_OR_RETURN(payload_bytes);
  for (const ComponentCookRecord& component : component_records) {
    Result<std::span<const std::byte>> ignored =
        content::checked_subspan(*payload_bytes, component.payload_offset, component.payload_size);
    REQUIRED_OR_RETURN(ignored);
  }
  for (const EntityCookRecord& entity : entities) {
    if (entity.component_begin > component_records.size() ||
        entity.component_count > component_records.size() - entity.component_begin) {
      return make_unexpected("entity component record range is out of bounds");
    }
  }

  return DecodedCookedScene{
      .strings = std::move(*strings),
      .modules = std::move(modules),
      .components = std::move(components),
      .scene_name = **scene_name,
      .entities = std::move(entities),
      .component_records = std::move(component_records),
      .payloads = *payload_bytes,
  };
}

}  // namespace

Result<ordered_json> dump_cooked_scene_to_json(const SceneSerializationContext& serialization,
                                               std::span<const std::byte> bytes) {
  Result<DecodedCookedScene> decoded = decode_cooked_scene_header(serialization, bytes);
  REQUIRED_OR_RETURN(decoded);

  ordered_json required_modules = ordered_json::array();
  for (const ModuleUse& module : decoded->modules) {
    ordered_json module_json;
    module_json["id"] = module.id;
    module_json["version"] = module.version;
    required_modules.push_back(std::move(module_json));
  }

  ordered_json required_components = ordered_json::object();
  for (const ComponentUse& component : decoded->components) {
    required_components[component.key] = component.schema_version;
  }

  ordered_json entities = ordered_json::array();
  for (const EntityCookRecord& entity : decoded->entities) {
    ordered_json entity_json;
    entity_json["guid"] = entity_guid_lower_hex(entity.guid);
    if (entity.name_index != k_invalid_string_index) {
      Result<const std::string*> name = string_at(decoded->strings, entity.name_index, "entity");
      REQUIRED_OR_RETURN(name);
      entity_json["name"] = **name;
    }
    ordered_json components = ordered_json::object();
    for (uint32_t i = 0; i < entity.component_count; ++i) {
      const ComponentCookRecord& component_record =
          decoded->component_records[entity.component_begin + i];
      Result<const std::string*> component_key =
          string_at(decoded->strings, component_record.component_key_index, "component record");
      REQUIRED_OR_RETURN(component_key);
      const scene::FrozenComponentRecord* component =
          serialization.find_authored_component(**component_key);
      ASSERT(component);
      Result<std::span<const std::byte>> payload_bytes = content::checked_subspan(
          decoded->payloads, component_record.payload_offset, component_record.payload_size);
      REQUIRED_OR_RETURN(payload_bytes);
      content::BinaryReader payload_reader(*payload_bytes);
      ordered_json payload = ordered_json::object();
      for (const scene::FrozenComponentFieldRecord& field : component->fields) {
        Result<json> value = decode_field_payload(payload_reader, field, decoded->strings);
        REQUIRED_OR_RETURN(value);
        payload[field.key] = ordered_json::parse(value->dump());
      }
      if (payload_reader.position() != payload_reader.size()) {
        return make_unexpected("component payload has trailing bytes");
      }
      components[**component_key] = std::move(payload);
    }
    entity_json["components"] = std::move(components);
    entities.push_back(std::move(entity_json));
  }

  ordered_json schema;
  schema["required_modules"] = std::move(required_modules);
  schema["required_components"] = std::move(required_components);
  ordered_json scene;
  scene["name"] = decoded->scene_name;
  ordered_json root;
  root["scene_format_version"] = k_cooked_scene_json_format_version;
  root["schema"] = std::move(schema);
  root["scene"] = std::move(scene);
  root["entities"] = std::move(entities);

  const json check_json = json::parse(root.dump());
  Result<void, core::DiagnosticReport> validated =
      validate_scene_file_full_report(serialization, check_json);
  if (!validated) {
    return make_unexpected(validated.error().to_string());
  }
  return root;
}

Result<void> dump_cooked_scene_file(const SceneSerializationContext& serialization,
                                    const std::filesystem::path& input_path,
                                    const std::filesystem::path& output_path) {
  Result<std::vector<std::byte>> bytes = read_binary_file(input_path);
  REQUIRED_OR_RETURN(bytes);
  Result<ordered_json> scene_json = dump_cooked_scene_to_json(serialization, *bytes);
  REQUIRED_OR_RETURN(scene_json);
  return write_text_file(output_path, scene_json->dump(2) + "\n");
}

Result<void> deserialize_cooked_scene(SceneManager& scenes,
                                      const SceneSerializationContext& serialization,
                                      std::span<const std::byte> bytes) {
  Result<ordered_json> scene_json = dump_cooked_scene_to_json(serialization, bytes);
  REQUIRED_OR_RETURN(scene_json);
  const json plain_json = json::parse(scene_json->dump());
  return deserialize_scene_json(scenes, serialization, plain_json);
}

Result<void> load_cooked_scene_file_no_json(Scene& scene,
                                            const SceneSerializationContext& serialization,
                                            const std::filesystem::path& path) {
  const Result<std::vector<std::byte>> bytes = read_binary_file(path);
  REQUIRED_OR_RETURN(bytes);
  const Result<DecodedCookedScene> decoded = decode_cooked_scene_header(serialization, *bytes);
  REQUIRED_OR_RETURN(decoded);

  for (const EntityCookRecord& entity : decoded->entities) {
    const flecs::entity flecs_entity = scene.create_entity(
        entity.guid,
        entity.name_index != k_invalid_string_index ? decoded->strings[entity.name_index] : "");
    for (uint32_t i = 0; i < entity.component_count; ++i) {
      const ComponentCookRecord& component_record =
          decoded->component_records[entity.component_begin + i];
      Result<const std::string*> component_key =
          string_at(decoded->strings, component_record.component_key_index, "component record");
      REQUIRED_OR_RETURN(component_key);
      const scene::FrozenComponentRecord* component =
          serialization.find_authored_component(**component_key);
      ASSERT(component, "component key {} is not registered", **component_key);
      ASSERT(component->ops.deserialize_cooked_fn,
             "component key {} is missing a deserialization binding", **component_key);
      content::BinaryReader payload_reader(decoded->payloads.subspan(
          component_record.payload_offset, component_record.payload_size));
      component->ops.deserialize_cooked_fn(flecs_entity, payload_reader);
    }
  }

  // Temporary compatibility fixup: render extraction currently expects LocalToWorld to be current
  // immediately after load. A dedicated transform propagation/dirty system should retire this.
  scene.world().each([](const Transform& transform, LocalToWorld& local_to_world) {
    local_to_world.value = transform_to_matrix(transform);
  });

  return {};
}

Result<SceneLoadResult> load_cooked_scene_file(SceneManager& scenes,
                                               const SceneSerializationContext& serialization,
                                               const std::filesystem::path& path) {
  Result<std::vector<std::byte>> bytes = read_binary_file(path);
  REQUIRED_OR_RETURN(bytes);
  Result<void> loaded = deserialize_cooked_scene(scenes, serialization, *bytes);
  REQUIRED_OR_RETURN(loaded);
  Scene* scene = scenes.active_scene();
  return SceneLoadResult{.scene_id = scene->id(), .scene = scene};
}

}  // namespace teng::engine
