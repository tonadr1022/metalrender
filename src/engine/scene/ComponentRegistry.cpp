#include "engine/scene/ComponentRegistry.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace teng::engine {

namespace {

constexpr uint64_t k_fnv_offset = 14695981039346656037ull;
constexpr uint64_t k_fnv_prime = 1099511628211ull;

[[nodiscard]] bool is_key_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

[[nodiscard]] bool is_namespaced_key(std::string_view key) {
  if (key.empty() || key.front() == '.' || key.back() == '.') {
    return false;
  }

  bool segment_has_char = false;
  bool saw_dot = false;
  for (const char c : key) {
    if (c == '.') {
      if (!segment_has_char) {
        return false;
      }
      saw_dot = true;
      segment_has_char = false;
      continue;
    }
    if (!is_key_char(c)) {
      return false;
    }
    segment_has_char = true;
  }

  return saw_dot && segment_has_char;
}

[[nodiscard]] bool is_field_key(std::string_view key) {
  if (key.empty()) {
    return false;
  }
  return std::ranges::all_of(key, is_key_char);
}

[[nodiscard]] core::DiagnosticPath module_path(std::string_view key) {
  core::DiagnosticPath path;
  path.object_key("schema").object_key("modules").object_key(std::string{key});
  return path;
}

[[nodiscard]] core::DiagnosticPath component_path(std::string_view key) {
  core::DiagnosticPath path;
  path.object_key("schema").object_key("components").object_key(std::string{key});
  return path;
}

[[nodiscard]] core::DiagnosticPath field_path(std::string_view component_key,
                                              std::string_view field_key) {
  core::DiagnosticPath path = component_path(component_key);
  path.object_key("fields").object_key(std::string{field_key});
  return path;
}

[[nodiscard]] bool module_key_less(const ComponentModuleDescriptor& a,
                                   const ComponentModuleDescriptor& b) {
  return a.key < b.key;
}

[[nodiscard]] bool component_key_less(const ComponentDescriptor& a, const ComponentDescriptor& b) {
  return a.key < b.key;
}

[[nodiscard]] bool field_key_less(const ComponentFieldDescriptor& a,
                                  const ComponentFieldDescriptor& b) {
  return a.key < b.key;
}

void validate_modules(std::vector<ComponentModuleDescriptor>& modules,
                      core::DiagnosticReport& diagnostics) {
  std::ranges::sort(modules, module_key_less);

  for (const ComponentModuleDescriptor& module : modules) {
    if (!is_namespaced_key(module.key)) {
      diagnostics.add_error(
          core::DiagnosticCode{"schema.invalid_module_key"}, module_path(module.key),
          std::format("module key '{}' is not a namespaced schema key", module.key));
    }
    if (module.version == 0) {
      diagnostics.add_error(core::DiagnosticCode{"schema.invalid_module_version"},
                            module_path(module.key), "module version must be greater than zero");
    }
  }

  for (size_t i = 1; i < modules.size(); ++i) {
    const ComponentModuleDescriptor& previous = modules[i - 1];
    const ComponentModuleDescriptor& current = modules[i];
    if (previous.key != current.key) {
      continue;
    }
    diagnostics.add_error(core::DiagnosticCode{"schema.duplicate_module"}, module_path(current.key),
                          std::format("module '{}' is registered more than once", current.key));
  }
}

void validate_component_fields(const ComponentDescriptor& component,
                               core::DiagnosticReport& diagnostics) {
  if (component.storage == ComponentStoragePolicy::Authored && component.fields.empty() &&
      !component.custom_codec) {
    diagnostics.add_error(
        core::DiagnosticCode{"schema.authored_component_without_fields"},
        component_path(component.key),
        std::format("authored component '{}' must declare fields or a custom codec",
                    component.key));
  }

  std::vector<ComponentFieldDescriptor> fields = component.fields;
  std::ranges::sort(fields, field_key_less);
  for (const ComponentFieldDescriptor& field : fields) {
    if (!is_field_key(field.key)) {
      diagnostics.add_error(
          core::DiagnosticCode{"schema.invalid_field_key"}, field_path(component.key, field.key),
          std::format("field key '{}' is not a valid schema field key", field.key));
    }
  }

  for (size_t i = 1; i < fields.size(); ++i) {
    const ComponentFieldDescriptor& previous = fields[i - 1];
    const ComponentFieldDescriptor& current = fields[i];
    if (previous.key != current.key) {
      continue;
    }
    diagnostics.add_error(core::DiagnosticCode{"schema.duplicate_field"},
                          field_path(component.key, current.key),
                          std::format("field '{}' is declared more than once", current.key));
  }
}

void validate_components(std::vector<ComponentDescriptor>& components,
                         const std::vector<ComponentModuleDescriptor>& modules,
                         core::DiagnosticReport& diagnostics) {
  std::ranges::sort(components, component_key_less);

  for (ComponentDescriptor& component : components) {
    if (!is_namespaced_key(component.key)) {
      diagnostics.add_error(
          core::DiagnosticCode{"schema.invalid_component_key"}, component_path(component.key),
          std::format("component key '{}' is not a namespaced schema key", component.key));
    }
    if (!is_namespaced_key(component.module_key)) {
      diagnostics.add_error(core::DiagnosticCode{"schema.invalid_module_key"},
                            component_path(component.key),
                            std::format("component '{}' references invalid module key '{}'",
                                        component.key, component.module_key));
    }
    if (std::ranges::find(modules, component.module_key, &ComponentModuleDescriptor::key) ==
        modules.end()) {
      diagnostics.add_error(core::DiagnosticCode{"schema.unknown_module"},
                            component_path(component.key),
                            std::format("component '{}' references unregistered module '{}'",
                                        component.key, component.module_key));
    }
    if (component.schema_version == 0) {
      diagnostics.add_error(core::DiagnosticCode{"schema.invalid_component_version"},
                            component_path(component.key),
                            "component schema version must be greater than zero");
    }

    component.cooked_id = component.cooked_id_override.value_or(stable_component_id(component.key));
    validate_component_fields(component, diagnostics);
  }

  for (size_t i = 1; i < components.size(); ++i) {
    const ComponentDescriptor& previous = components[i - 1];
    const ComponentDescriptor& current = components[i];
    if (previous.key != current.key) {
      continue;
    }
    diagnostics.add_error(core::DiagnosticCode{"schema.duplicate_component"},
                          component_path(current.key),
                          std::format("component '{}' is registered more than once", current.key));
  }

  std::vector<ComponentDescriptor> cooked_sorted = components;
  std::ranges::sort(cooked_sorted, {}, &ComponentDescriptor::cooked_id);
  for (size_t i = 1; i < cooked_sorted.size(); ++i) {
    const ComponentDescriptor& previous = cooked_sorted[i - 1];
    const ComponentDescriptor& current = cooked_sorted[i];
    if (previous.cooked_id != current.cooked_id) {
      continue;
    }
    diagnostics.add_error(
        core::DiagnosticCode{"schema.cooked_id_collision"}, component_path(current.key),
        std::format("component '{}' has the same cooked id as '{}'", current.key, previous.key));
  }
}

}  // namespace

ComponentDescriptorBuilder::ComponentDescriptorBuilder(std::string key) {
  descriptor_.key = std::move(key);
}

ComponentDescriptorBuilder& ComponentDescriptorBuilder::module(std::string key) {
  descriptor_.module_key = std::move(key);
  return *this;
}

ComponentDescriptorBuilder& ComponentDescriptorBuilder::schema_version(uint32_t version) {
  descriptor_.schema_version = version;
  return *this;
}

ComponentDescriptorBuilder& ComponentDescriptorBuilder::storage(ComponentStoragePolicy policy) {
  descriptor_.storage = policy;
  return *this;
}

ComponentDescriptorBuilder& ComponentDescriptorBuilder::custom_codec(bool enabled) {
  descriptor_.custom_codec = enabled;
  return *this;
}

ComponentDescriptorBuilder& ComponentDescriptorBuilder::cooked_id_for_testing(uint64_t cooked_id) {
  descriptor_.cooked_id_override = cooked_id;
  return *this;
}

ComponentDescriptorBuilder& ComponentDescriptorBuilder::field(std::string key) {
  descriptor_.fields.push_back(ComponentFieldDescriptor{.key = std::move(key)});
  return *this;
}

ComponentDescriptor ComponentDescriptorBuilder::build() { return std::move(descriptor_); }

ComponentRegistry::ComponentRegistry(std::vector<ComponentModuleDescriptor> modules,
                                     std::vector<ComponentDescriptor> components)
    : modules_(std::move(modules)), components_(std::move(components)) {}

const ComponentModuleDescriptor* ComponentRegistry::find_module(std::string_view key) const {
  const auto it = std::ranges::find(modules_, key, &ComponentModuleDescriptor::key);
  return it == modules_.end() ? nullptr : &*it;
}

const ComponentDescriptor* ComponentRegistry::find_component(std::string_view key) const {
  const auto it = std::ranges::find(components_, key, &ComponentDescriptor::key);
  return it == components_.end() ? nullptr : &*it;
}

const ComponentDescriptor* ComponentRegistry::find_component_by_cooked_id(
    uint64_t cooked_id) const {
  const auto it = std::ranges::find(components_, cooked_id, &ComponentDescriptor::cooked_id);
  return it == components_.end() ? nullptr : &*it;
}

ComponentRegistryBuilder& ComponentRegistryBuilder::module(std::string key, uint32_t version) {
  modules_.push_back(ComponentModuleDescriptor{.key = std::move(key), .version = version});
  return *this;
}

ComponentRegistryBuilder& ComponentRegistryBuilder::component(ComponentDescriptor descriptor) {
  components_.push_back(std::move(descriptor));
  return *this;
}

ComponentRegistryFreezeResult ComponentRegistryBuilder::freeze() const {
  ComponentRegistryFreezeResult result;
  std::vector<ComponentModuleDescriptor> modules = modules_;
  std::vector<ComponentDescriptor> components = components_;

  validate_modules(modules, result.diagnostics);
  validate_components(components, modules, result.diagnostics);

  if (!result.diagnostics.has_errors()) {
    result.registry = ComponentRegistry{std::move(modules), std::move(components)};
  }

  return result;
}

uint64_t stable_component_id(std::string_view component_key) {
  uint64_t hash = k_fnv_offset;
  for (const char c : component_key) {
    hash ^= static_cast<uint8_t>(c);
    hash *= k_fnv_prime;
  }
  return hash;
}

void register_core_components(ComponentRegistryBuilder& builder) {
  builder.module("teng.core", 1);
  builder
      .component(ComponentDescriptorBuilder{"teng.core.transform"}
                     .module("teng.core")
                     .storage(ComponentStoragePolicy::Authored)
                     .field("translation")
                     .field("rotation")
                     .field("scale")
                     .build())
      .component(ComponentDescriptorBuilder{"teng.core.camera"}
                     .module("teng.core")
                     .storage(ComponentStoragePolicy::Authored)
                     .field("fov_y")
                     .field("z_near")
                     .field("z_far")
                     .field("primary")
                     .build())
      .component(ComponentDescriptorBuilder{"teng.core.directional_light"}
                     .module("teng.core")
                     .storage(ComponentStoragePolicy::Authored)
                     .field("direction")
                     .field("color")
                     .field("intensity")
                     .build())
      .component(ComponentDescriptorBuilder{"teng.core.mesh_renderable"}
                     .module("teng.core")
                     .storage(ComponentStoragePolicy::Authored)
                     .field("model")
                     .build())
      .component(ComponentDescriptorBuilder{"teng.core.sprite_renderable"}
                     .module("teng.core")
                     .storage(ComponentStoragePolicy::Authored)
                     .field("texture")
                     .field("tint")
                     .field("sorting_layer")
                     .field("sorting_order")
                     .build())
      .component(ComponentDescriptorBuilder{"teng.core.local_to_world"}
                     .module("teng.core")
                     .storage(ComponentStoragePolicy::RuntimeDerived)
                     .build())
      .component(ComponentDescriptorBuilder{"teng.core.fps_camera_controller"}
                     .module("teng.core")
                     .storage(ComponentStoragePolicy::RuntimeSession)
                     .field("pitch")
                     .field("yaw")
                     .field("max_velocity")
                     .field("move_speed")
                     .field("mouse_sensitivity")
                     .field("look_pitch_sign")
                     .field("mouse_captured")
                     .build())
      .component(ComponentDescriptorBuilder{"teng.core.engine_input_snapshot"}
                     .module("teng.core")
                     .storage(ComponentStoragePolicy::RuntimeSession)
                     .build());
}

}  // namespace teng::engine
