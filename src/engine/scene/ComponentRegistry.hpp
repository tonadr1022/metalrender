#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/Diagnostic.hpp"

namespace teng::engine {

enum class ComponentStoragePolicy : uint8_t {
  Authored,
  RuntimeDerived,
  RuntimeSession,
  EditorOnly,
};

struct ComponentModuleDescriptor {
  std::string key;
  uint32_t version{1};
};

struct ComponentFieldDescriptor {
  std::string key;
};

struct ComponentDescriptor {
  std::string key;
  std::string module_key;
  uint32_t schema_version{1};
  ComponentStoragePolicy storage{ComponentStoragePolicy::Authored};
  bool custom_codec{false};
  std::optional<uint64_t> cooked_id_override;
  uint64_t cooked_id{};
  std::vector<ComponentFieldDescriptor> fields;
};

class ComponentDescriptorBuilder {
 public:
  explicit ComponentDescriptorBuilder(std::string key);

  ComponentDescriptorBuilder& module(std::string key);
  ComponentDescriptorBuilder& schema_version(uint32_t version);
  ComponentDescriptorBuilder& storage(ComponentStoragePolicy policy);
  ComponentDescriptorBuilder& custom_codec(bool enabled = true);
  ComponentDescriptorBuilder& cooked_id_for_testing(uint64_t cooked_id);
  ComponentDescriptorBuilder& field(std::string key);

  [[nodiscard]] ComponentDescriptor build();

 private:
  ComponentDescriptor descriptor_;
};

class ComponentRegistry {
 public:
  explicit ComponentRegistry(std::vector<ComponentModuleDescriptor> modules,
                             std::vector<ComponentDescriptor> components);

  [[nodiscard]] const std::vector<ComponentModuleDescriptor>& modules() const { return modules_; }
  [[nodiscard]] const std::vector<ComponentDescriptor>& components() const { return components_; }

  [[nodiscard]] const ComponentModuleDescriptor* find_module(std::string_view key) const;
  [[nodiscard]] const ComponentDescriptor* find_component(std::string_view key) const;
  [[nodiscard]] const ComponentDescriptor* find_component_by_cooked_id(uint64_t cooked_id) const;

 private:
  std::vector<ComponentModuleDescriptor> modules_;
  std::vector<ComponentDescriptor> components_;
};

struct ComponentRegistryFreezeResult {
  core::DiagnosticReport diagnostics;
  std::optional<ComponentRegistry> registry;
};

class ComponentRegistryBuilder {
 public:
  ComponentRegistryBuilder& module(std::string key, uint32_t version);
  ComponentRegistryBuilder& component(ComponentDescriptor descriptor);

  [[nodiscard]] ComponentRegistryFreezeResult freeze() const;

 private:
  std::vector<ComponentModuleDescriptor> modules_;
  std::vector<ComponentDescriptor> components_;
};

[[nodiscard]] uint64_t stable_component_id(std::string_view component_key);

void register_core_components(ComponentRegistryBuilder& builder);

}  // namespace teng::engine
