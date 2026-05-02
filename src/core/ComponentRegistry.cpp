#include "core/ComponentRegistry.hpp"

#include <algorithm>
#include <ranges>
#include <unordered_map>

namespace TENG_NAMESPACE::core {

namespace {

[[nodiscard]] DiagnosticPath path_modules_key(std::string_view module_id) {
  DiagnosticPath path;
  path.object_key("modules").object_key(std::string{module_id});
  return path;
}

[[nodiscard]] DiagnosticPath path_components_key(std::string_view component_key) {
  DiagnosticPath path;
  path.object_key("components").object_key(std::string{component_key});
  return path;
}

[[nodiscard]] DiagnosticPath path_components_field(std::string_view component_key,
                                                   std::string_view field_key) {
  DiagnosticPath path;
  path.object_key("components")
      .object_key(std::string{component_key})
      .object_key("fields")
      .object_key(std::string{field_key});
  return path;
}

}  // namespace

const FrozenComponentRecord* ComponentRegistry::find(std::string_view component_key) const {
  const auto it = std::ranges::lower_bound(components_, component_key, std::less{},
                                           &FrozenComponentRecord::component_key);
  if (it == components_.end() || it->component_key != component_key) {
    return nullptr;
  }
  return &*it;
}

std::optional<uint64_t> ComponentRegistry::stable_component_id(
    std::string_view component_key) const {
  const FrozenComponentRecord* record = find(component_key);
  if (!record) {
    return std::nullopt;
  }
  return record->stable_id;
}

void ComponentRegistryBuilder::register_module(std::string module_id, uint32_t version) {
  modules_.emplace_back(std::move(module_id), version);
}

void ComponentRegistryBuilder::register_component(ComponentRegistration component) {
  components_.push_back(std::move(component));
}

bool ComponentRegistryBuilder::try_freeze(ComponentRegistry& out, DiagnosticReport& report) const {
  out = ComponentRegistry{};

  if (modules_.empty() && components_.empty()) {
    return true;
  }

  std::vector<std::pair<std::string, uint32_t>> sorted_modules = modules_;
  std::ranges::sort(sorted_modules, {}, &std::pair<std::string, uint32_t>::first);

  std::unordered_map<std::string, uint32_t> resolved_module_version;
  resolved_module_version.reserve(sorted_modules.size());

  for (size_t i = 0; i < sorted_modules.size();) {
    size_t j = i + 1;
    while (j < sorted_modules.size() && sorted_modules[j].first == sorted_modules[i].first) {
      ++j;
    }
    const std::string_view module_id = sorted_modules[i].first;
    bool uniform_version = true;
    const uint32_t v0 = sorted_modules[i].second;
    for (size_t k = i + 1; k < j; ++k) {
      if (sorted_modules[k].second != v0) {
        uniform_version = false;
        break;
      }
    }
    if (j - i > 1) {
      const DiagnosticPath path = path_modules_key(module_id);
      if (uniform_version) {
        report.add_error(DiagnosticCode{"schema.duplicate_module"}, path,
                         "module registered more than once");
      } else {
        report.add_error(DiagnosticCode{"schema.module_version_mismatch"}, path,
                         "conflicting versions for the same module id");
      }
    } else {
      resolved_module_version.emplace(sorted_modules[i].first, sorted_modules[i].second);
    }
    i = j;
  }

  std::vector<ComponentRegistration> sorted_components = components_;
  std::ranges::sort(sorted_components, {}, &ComponentRegistration::component_key);

  for (size_t i = 0; i < sorted_components.size();) {
    size_t j = i + 1;
    while (j < sorted_components.size() &&
           sorted_components[j].component_key == sorted_components[i].component_key) {
      ++j;
    }
    if (j - i > 1) {
      report.add_error(DiagnosticCode{"schema.duplicate_component_key"},
                       path_components_key(sorted_components[i].component_key),
                       "duplicate component key in registry builder");
    }
    i = j;
  }

  for (const ComponentRegistration& component : sorted_components) {
    const DiagnosticPath comp_path = path_components_key(component.component_key);

    const auto module_it = resolved_module_version.find(component.module_id);
    if (module_it == resolved_module_version.end()) {
      report.add_error(DiagnosticCode{"schema.unknown_module"}, comp_path,
                       "component references a module that is missing or invalid");
      continue;
    }
    if (module_it->second != component.module_version) {
      report.add_error(DiagnosticCode{"schema.component_module_version_mismatch"}, comp_path,
                       "component module_version does not match registered module");
    }

    if (component.default_on_create && component.storage != ComponentStoragePolicy::Authored) {
      report.add_error(DiagnosticCode{"schema.invalid_storage_policy"}, comp_path,
                       "default_on_create requires Authored storage policy");
    }

    if (component.field_keys.size() > 1) {
      std::vector<std::string> keys = component.field_keys;
      std::ranges::sort(keys);
      const auto dup = std::ranges::adjacent_find(keys);
      if (dup != keys.end()) {
        report.add_error(DiagnosticCode{"schema.duplicate_field_key"},
                         path_components_field(component.component_key, *dup),
                         "duplicate field key in component schema");
      }
    }
  }

  if (report.has_errors()) {
    return false;
  }

  std::vector<FrozenComponentRecord> frozen;
  frozen.reserve(sorted_components.size());

  std::unordered_map<uint64_t, std::string> id_to_key;
  id_to_key.reserve(sorted_components.size());

  for (const ComponentRegistration& component : sorted_components) {
    const uint64_t sid = stable_component_id_v1(component.component_key);
    const auto [it, inserted] = id_to_key.emplace(sid, component.component_key);
    if (!inserted) {
      report.add_error(DiagnosticCode{"schema.duplicate_stable_component_id"},
                       path_components_key(component.component_key),
                       "stable component id collision with another component key");
      report.add_error(DiagnosticCode{"schema.duplicate_stable_component_id"},
                       path_components_key(it->second),
                       "stable component id collision with another component key");
      out = ComponentRegistry{};
      return false;
    }

    frozen.push_back(FrozenComponentRecord{
        .component_key = component.component_key,
        .module_id = component.module_id,
        .module_version = component.module_version,
        .schema_version = component.schema_version,
        .storage = component.storage,
        .default_on_create = component.default_on_create,
        .field_keys = component.field_keys,
        .stable_id = sid,
    });
  }

  out.components_ = std::move(frozen);
  return true;
}

}  // namespace TENG_NAMESPACE::core
