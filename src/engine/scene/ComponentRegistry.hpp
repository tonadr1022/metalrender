#pragma once

#include <flecs.h>

#include <array>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/Diagnostic.hpp"

namespace teng::engine::content {
class BinaryReader;
}  // namespace teng::engine::content

namespace teng::engine::scene {

enum class ComponentStoragePolicy : uint8_t {
  Authored,
  RuntimeDerived,
  RuntimeSession,
  EditorOnly,
};

[[nodiscard]] const char* component_storage_policy_to_string(ComponentStoragePolicy policy);

enum class ComponentSchemaVisibility : uint8_t {
  Editable,
  DebugInspectable,
  Hidden,
};

[[nodiscard]] const char* component_schema_visibility_to_string(
    ComponentSchemaVisibility visibility);

[[nodiscard]] constexpr uint64_t stable_component_id_v1(std::string_view component_key) noexcept {
  constexpr uint64_t offset_basis = 14695981039346656037ULL;
  constexpr uint64_t prime = 1099511628211ULL;
  uint64_t hash = offset_basis;
  for (const unsigned char byte : component_key) {
    hash ^= byte;
    hash *= prime;
  }
  return hash;
}

enum class ComponentFieldKind : uint32_t {
  Bool,
  I32,
  U32,
  F32,
  String,
  Vec2,
  Vec3,
  Vec4,
  Quat,
  Mat4,
  AssetId,
  Enum,
};

[[nodiscard]] const char* component_field_kind_to_string(ComponentFieldKind kind);

struct ComponentDefaultVec2 {
  float x{}, y{};
};

struct ComponentDefaultVec3 {
  float x{}, y{}, z{};
};

struct ComponentDefaultVec4 {
  float x{}, y{}, z{}, w{};
};

struct ComponentDefaultQuat {
  float w{1.f}, x{}, y{}, z{};
};

struct ComponentDefaultMat4 {
  std::array<float, 16> elements{};
};

struct ComponentDefaultAssetId {
  std::string value;
};

struct ComponentDefaultEnum {
  int64_t value{};
};

using ComponentFieldDefaultValue =
    std::variant<bool, int64_t, uint64_t, float, std::string, ComponentDefaultVec2,
                 ComponentDefaultVec3, ComponentDefaultVec4, ComponentDefaultQuat,
                 ComponentDefaultMat4, ComponentDefaultAssetId, ComponentDefaultEnum>;

struct ComponentAssetFieldMetadata {
  std::string expected_kind;
};

struct ComponentEnumValueRegistration {
  std::string key;
  int64_t value{};
};

struct ComponentEnumRegistration {
  std::string enum_key;
  std::vector<ComponentEnumValueRegistration> values;
};

struct FrozenComponentRecord;

using ComponentSchemaValidationHook = void (*)(const FrozenComponentRecord& component,
                                               core::DiagnosticReport& report);
using RegisterFlecsFn = void (*)(flecs::world&);
using ApplyOnCreateFn = void (*)(flecs::entity);
using SerializeComponentFn = nlohmann::json (*)(flecs::entity entity);
using DeserializeComponentFn = void (*)(flecs::entity entity, const nlohmann::json& payload);
using DeserializeCookedComponentFn = void (*)(flecs::entity entity, content::BinaryReader& reader);
using HasComponentFn = bool (*)(flecs::entity entity);
using RemoveComponentFn = void (*)(flecs::entity entity);

enum class ScriptExposure : uint8_t {
  None,
  Read,
  ReadWrite,
};

struct ComponentFieldDescriptor {
  std::string_view key;
  std::string_view member_name;
  ComponentFieldKind kind{};
  bool authored_required{true};
  ComponentFieldDefaultValue default_value;
  std::optional<ComponentAssetFieldMetadata> asset;
  std::optional<ComponentEnumRegistration> enumeration;
  ScriptExposure script_exposure{ScriptExposure::None};
};

struct ComponentTypeOps {
  RegisterFlecsFn register_flecs_fn{};
  ApplyOnCreateFn apply_on_create_fn{};
  HasComponentFn has_component_fn{};
  SerializeComponentFn serialize_fn{};
  DeserializeComponentFn deserialize_fn{};
  DeserializeCookedComponentFn deserialize_cooked_fn{};
  RemoveComponentFn remove_fn{};
};

struct ComponentDescriptor {
  std::string_view component_key;
  uint32_t schema_version{1};
  ComponentStoragePolicy storage{ComponentStoragePolicy::Authored};
  ComponentSchemaVisibility visibility{ComponentSchemaVisibility::Editable};
  bool add_on_create{};
  ComponentSchemaValidationHook schema_validation_hook{nullptr};
  std::span<const ComponentFieldDescriptor> fields;
  ComponentTypeOps ops;
};

struct ComponentModuleDescriptor {
  std::string_view module_id;
  uint32_t module_version{1};
  std::span<const ComponentDescriptor> components;
};

struct FrozenComponentFieldRecord {
  std::string key;
  std::string member_name;
  ComponentFieldKind kind{};
  bool authored_required{true};
  ComponentFieldDefaultValue default_value;
  std::optional<ComponentAssetFieldMetadata> asset;
  std::optional<ComponentEnumRegistration> enumeration;
  ScriptExposure script_exposure{ScriptExposure::None};
};

struct FrozenModuleRecord {
  std::string module_id;
  uint32_t version{};

  bool operator==(const FrozenModuleRecord& other) const {
    return module_id == other.module_id && version == other.version;
  }
};

struct FrozenComponentRecord {
  std::string component_key;
  std::string module_id;
  uint32_t module_version{};
  uint32_t schema_version{};
  ComponentStoragePolicy storage{ComponentStoragePolicy::Authored};
  ComponentSchemaVisibility visibility{ComponentSchemaVisibility::Editable};
  bool add_on_create{};
  ComponentSchemaValidationHook schema_validation_hook{nullptr};
  std::vector<FrozenComponentFieldRecord> fields;
  ComponentTypeOps ops;
  uint64_t stable_id{};
};

class ComponentRegistry {
 public:
  ComponentRegistry() = default;

  [[nodiscard]] const FrozenComponentRecord* find(std::string_view component_key) const;
  [[nodiscard]] const FrozenModuleRecord* find_module(std::string_view module_id) const;
  [[nodiscard]] std::optional<uint64_t> stable_component_id(std::string_view component_key) const;

  [[nodiscard]] const std::vector<FrozenModuleRecord>& modules() const { return modules_; }
  [[nodiscard]] const std::vector<FrozenComponentRecord>& components() const { return components_; }

 private:
  friend bool try_freeze_component_registry(std::span<const ComponentModuleDescriptor> modules,
                                            ComponentRegistry& out, core::DiagnosticReport& report);

  std::vector<FrozenModuleRecord> modules_;
  std::vector<FrozenComponentRecord> components_;
};

[[nodiscard]] bool try_freeze_component_registry(std::span<const ComponentModuleDescriptor> modules,
                                                 ComponentRegistry& out,
                                                 core::DiagnosticReport& report);

}  // namespace teng::engine::scene
