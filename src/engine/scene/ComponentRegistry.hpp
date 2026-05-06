#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/Diagnostic.hpp"

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
  std::string key;
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

struct ComponentFieldRegistration {
  std::string key;
  ComponentFieldKind kind{};
  bool authored_required{true};
  std::optional<ComponentFieldDefaultValue> default_value;
  std::optional<ComponentAssetFieldMetadata> asset;
  std::optional<ComponentEnumRegistration> enumeration;
};

struct ComponentRegistration {
  std::string component_key;
  std::string module_id;
  uint32_t module_version{1};
  uint32_t schema_version{1};
  ComponentStoragePolicy storage{ComponentStoragePolicy::Authored};
  ComponentSchemaVisibility visibility{ComponentSchemaVisibility::Editable};
  bool add_on_create{};
  ComponentSchemaValidationHook schema_validation_hook{nullptr};
  std::vector<ComponentFieldRegistration> fields;
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
  std::vector<ComponentFieldRegistration> fields;
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
  friend class ComponentRegistryBuilder;

  std::vector<FrozenModuleRecord> modules_;
  std::vector<FrozenComponentRecord> components_;
};

class ComponentRegistryBuilder {
 public:
  void register_module(std::string module_id, uint32_t version);
  void register_component(ComponentRegistration component);

  /// Builds a frozen registry from trusted first-party/generated registrations.
  /// Internal component field schema invariants assert; registry composition conflicts and
  /// validation hooks append diagnostics to `report`.
  [[nodiscard]] bool try_freeze(ComponentRegistry& out, core::DiagnosticReport& report) const;

  [[nodiscard]] const std::vector<std::pair<std::string, uint32_t>>& modules() const {
    return modules_;
  }
  [[nodiscard]] const std::vector<ComponentRegistration>& components() const { return components_; }
  [[nodiscard]] const ComponentRegistration* find(std::string_view component_key) const;

 private:
  std::vector<std::pair<std::string, uint32_t>> modules_;
  std::vector<ComponentRegistration> components_;
};

}  // namespace teng::engine::scene
