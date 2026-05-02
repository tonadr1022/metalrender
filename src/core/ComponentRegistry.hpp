#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Config.hpp"
#include "core/Diagnostic.hpp"

namespace TENG_NAMESPACE::core {

enum class ComponentStoragePolicy : uint8_t {
  Authored,
  RuntimeDerived,
  RuntimeSession,
  EditorOnly,
};

[[nodiscard]] constexpr uint64_t stable_component_id_v1(std::string_view component_key) noexcept {
  constexpr uint64_t offset_basis = 14695981039346656037ULL;
  constexpr uint64_t prime = 1099511628211ULL;
  uint64_t hash = offset_basis;
  for (unsigned char byte : component_key) {
    hash ^= byte;
    hash *= prime;
  }
  return hash;
}

enum class ComponentFieldKind : uint32_t {
  Bool,
  I32,
  F32,
  String,
  Vec2,
  Vec3,
  Vec4,
  Quat,
  Mat4,
  AssetId,
};

struct ComponentFieldRegistration {
  std::string key;
  ComponentFieldKind kind;
  bool required{true};
  bool default_on_create{false};
};

struct ComponentRegistration {
  std::string component_key;
  std::string module_id;
  uint32_t module_version{1};
  uint32_t schema_version{1};
  ComponentStoragePolicy storage{ComponentStoragePolicy::Authored};
  bool default_on_create{false};
  std::vector<ComponentFieldRegistration> fields;
};

struct FrozenComponentRecord {
  std::string component_key;
  std::string module_id;
  uint32_t module_version{};
  uint32_t schema_version{};
  ComponentStoragePolicy storage{ComponentStoragePolicy::Authored};
  bool default_on_create{false};
  std::vector<ComponentFieldRegistration> fields;
  uint64_t stable_id{};
};

class ComponentRegistry {
 public:
  ComponentRegistry() = default;

  [[nodiscard]] const FrozenComponentRecord* find(std::string_view component_key) const;
  [[nodiscard]] std::optional<uint64_t> stable_component_id(std::string_view component_key) const;

  [[nodiscard]] const std::vector<FrozenComponentRecord>& components() const { return components_; }

 private:
  friend class ComponentRegistryBuilder;

  std::vector<FrozenComponentRecord> components_;
};

class ComponentRegistryBuilder {
 public:
  void register_module(std::string module_id, uint32_t version);
  void register_component(ComponentRegistration component);

  /// Builds a frozen registry when there are no errors. On failure, `out` is cleared and
  /// diagnostics are appended to `report`.
  [[nodiscard]] bool try_freeze(ComponentRegistry& out, DiagnosticReport& report) const;

  [[nodiscard]] const std::vector<std::pair<std::string, uint32_t>>& modules() const {
    return modules_;
  }
  [[nodiscard]] const std::vector<ComponentRegistration>& components() const { return components_; }

 private:
  std::vector<std::pair<std::string, uint32_t>> modules_;
  std::vector<ComponentRegistration> components_;
};

}  // namespace TENG_NAMESPACE::core
