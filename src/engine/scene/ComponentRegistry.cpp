#include "ComponentRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <span>
#include <unordered_map>
#include <unordered_set>

#include "core/EAssert.hpp"

namespace teng::engine::scene {

using DiagnosticPath = core::DiagnosticPath;
using DiagnosticReport = core::DiagnosticReport;
using DiagnosticCode = core::DiagnosticCode;

const char* component_storage_policy_to_string(ComponentStoragePolicy policy) {
  switch (policy) {
    case ComponentStoragePolicy::Authored:
      return "authored";
    case ComponentStoragePolicy::RuntimeDerived:
      return "runtime_derived";
    case ComponentStoragePolicy::RuntimeSession:
      return "runtime_session";
    case ComponentStoragePolicy::EditorOnly:
      return "editor_only";
  }
}

const char* component_schema_visibility_to_string(ComponentSchemaVisibility visibility) {
  switch (visibility) {
    case ComponentSchemaVisibility::Editable:
      return "editable";
    case ComponentSchemaVisibility::DebugInspectable:
      return "debug_inspectable";
    case ComponentSchemaVisibility::Hidden:
      return "hidden";
  }
}

const char* component_field_kind_to_string(ComponentFieldKind kind) {
  switch (kind) {
    case ComponentFieldKind::Bool:
      return "bool";
    case ComponentFieldKind::I32:
      return "i32";
    case ComponentFieldKind::U32:
      return "u32";
    case ComponentFieldKind::F32:
      return "f32";
    case ComponentFieldKind::String:
      return "string";
    case ComponentFieldKind::Vec2:
      return "vec2";
    case ComponentFieldKind::Vec3:
      return "vec3";
    case ComponentFieldKind::Vec4:
      return "vec4";
    case ComponentFieldKind::Quat:
      return "quat";
    case ComponentFieldKind::Mat4:
      return "mat4";
    case ComponentFieldKind::AssetId:
      return "asset_id";
    case ComponentFieldKind::Enum:
      return "enum";
  }
}

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

[[nodiscard]] bool is_valid_asset_expected_kind(std::string_view kind) {
  if (kind.empty()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(kind[0]);
  if (first < 'a' || first > 'z') {
    return false;
  }
  for (size_t i = 1; i < kind.size(); ++i) {
    const auto c = static_cast<unsigned char>(kind[i]);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
      continue;
    }
    return false;
  }
  return true;
}

void validate_component_fields(std::string_view component_key,
                               std::span<const ComponentFieldDescriptor> fields) {
  for (const ComponentFieldDescriptor& field : fields) {
    ASSERT(!field.key.empty(), "Component schema '{}' has an empty field key", component_key);
  }

  if (fields.size() > 1) {
    std::vector<std::string_view> keys;
    keys.reserve(fields.size());
    for (const ComponentFieldDescriptor& field : fields) {
      keys.push_back(field.key);
    }
    std::ranges::sort(keys);
    const auto dup = std::ranges::adjacent_find(keys);
    ASSERT(dup == keys.end(), "Component schema '{}' registers duplicate field key '{}'",
           component_key, *dup);
  }

  for (const ComponentFieldDescriptor& field : fields) {
    if (field.asset.has_value()) {
      ASSERT(field.kind == ComponentFieldKind::AssetId,
             "Component schema '{}.{}' has asset metadata on a '{}' field", component_key,
             field.key, component_field_kind_to_string(field.kind));
      const std::string& expected = field.asset->expected_kind;
      ASSERT(is_valid_asset_expected_kind(expected),
             "Component schema '{}.{}' has invalid asset expected_kind '{}'", component_key,
             field.key, expected);
    }

    if (field.kind == ComponentFieldKind::Enum) {
      const auto& enum_opt = field.enumeration;
      if (!enum_opt.has_value()) {
        ASSERT(false,
               "Component schema '{}.{}' is an enum field without enum metadata", component_key,
               field.key);
      } else {
        const ComponentEnumRegistration& en = *enum_opt;
        ASSERT(!en.enum_key.empty(), "Component schema '{}.{}' has an empty enum_key", component_key,
               field.key);
        ASSERT(!en.values.empty(), "Component schema '{}.{}' declares an enum with no values",
               component_key, field.key);

        std::unordered_set<std::string> value_keys;
        std::unordered_set<int64_t> numeric_values;
        for (const ComponentEnumValueRegistration& ev : en.values) {
          ASSERT(!ev.key.empty(), "Component schema '{}.{}' has an enum value with an empty key",
                 component_key, field.key);
          const auto inserted_key = value_keys.insert(ev.key);
          ASSERT(inserted_key.second,
                 "Component schema '{}.{}' registers duplicate enum value key '{}'", component_key,
                 field.key, ev.key);
          const auto inserted_number = numeric_values.insert(ev.value);
          ASSERT(inserted_number.second,
                 "Component schema '{}.{}' registers duplicate enum numeric value {}",
                 component_key, field.key, ev.value);
        }
      }
    } else if (field.enumeration.has_value()) {
      ASSERT(false, "Component schema '{}.{}' has enum metadata on a '{}' field", component_key,
             field.key, component_field_kind_to_string(field.kind));
    }
  }
}

void validate_component_ops(const ComponentModuleDescriptor& module,
                            const ComponentDescriptor& component, DiagnosticReport& report) {
  const DiagnosticPath comp_path = path_components_key(component.component_key);
  if (component.storage != ComponentStoragePolicy::EditorOnly &&
      component.ops.register_flecs_fn == nullptr) {
    report.add_error(DiagnosticCode{"schema.missing_register_flecs_fn"}, comp_path,
                     "non-EditorOnly component is missing register_flecs_fn");
  }

  if (component.add_on_create && component.ops.apply_on_create_fn == nullptr) {
    report.add_error(DiagnosticCode{"schema.missing_apply_on_create_fn"}, comp_path,
                     "add_on_create component is missing apply_on_create_fn");
  }

  if (component.storage == ComponentStoragePolicy::Authored) {
    if (component.ops.has_component_fn == nullptr) {
      report.add_error(DiagnosticCode{"schema.missing_has_component_fn"}, comp_path,
                       "Authored component is missing has_component_fn");
    }
    if (component.ops.serialize_fn == nullptr) {
      report.add_error(DiagnosticCode{"schema.missing_serialize_component_fn"}, comp_path,
                       "Authored component is missing serialize_fn");
    }
    if (component.ops.deserialize_fn == nullptr) {
      report.add_error(DiagnosticCode{"schema.missing_deserialize_component_fn"}, comp_path,
                       "Authored component is missing deserialize_fn");
    }
  }

  (void)module;
}

[[nodiscard]] FrozenComponentFieldRecord freeze_field(const ComponentFieldDescriptor& field) {
  return FrozenComponentFieldRecord{
      .key = std::string{field.key},
      .member_name = std::string{field.member_name},
      .kind = field.kind,
      .authored_required = field.authored_required,
      .default_value = field.default_value,
      .asset = field.asset,
      .enumeration = field.enumeration,
      .script_exposure = field.script_exposure,
  };
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

const FrozenModuleRecord* ComponentRegistry::find_module(std::string_view module_id) const {
  const auto it =
      std::ranges::lower_bound(modules_, module_id, std::less{}, &FrozenModuleRecord::module_id);
  if (it == modules_.end() || it->module_id != module_id) {
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

bool try_freeze_component_registry(std::span<const ComponentModuleDescriptor> modules,
                                   ComponentRegistry& out, DiagnosticReport& report) {
  out = ComponentRegistry{};

  if (modules.empty()) {
    return true;
  }

  std::vector<const ComponentModuleDescriptor*> sorted_modules;
  sorted_modules.reserve(modules.size());
  for (const ComponentModuleDescriptor& module : modules) {
    sorted_modules.push_back(&module);
  }
  std::ranges::sort(sorted_modules, {},
                    [](const ComponentModuleDescriptor* module) { return module->module_id; });

  std::unordered_map<std::string, uint32_t> resolved_module_version;
  resolved_module_version.reserve(sorted_modules.size());

  for (size_t i = 0; i < sorted_modules.size();) {
    size_t j = i + 1;
    while (j < sorted_modules.size() &&
           sorted_modules[j]->module_id == sorted_modules[i]->module_id) {
      ++j;
    }
    const std::string_view module_id = sorted_modules[i]->module_id;
    bool uniform_version = true;
    const uint32_t v0 = sorted_modules[i]->module_version;
    for (size_t k = i + 1; k < j; ++k) {
      if (sorted_modules[k]->module_version != v0) {
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
      resolved_module_version.emplace(sorted_modules[i]->module_id,
                                      sorted_modules[i]->module_version);
    }
    i = j;
  }

  struct ComponentRef {
    const ComponentModuleDescriptor* module{};
    const ComponentDescriptor* component{};
  };
  std::vector<ComponentRef> sorted_components;
  for (const ComponentModuleDescriptor& module : modules) {
    for (const ComponentDescriptor& component : module.components) {
      sorted_components.push_back(ComponentRef{.module = &module, .component = &component});
    }
  }
  std::ranges::sort(sorted_components, {},
                    [](const ComponentRef& ref) { return ref.component->component_key; });

  for (size_t i = 0; i < sorted_components.size();) {
    size_t j = i + 1;
    while (j < sorted_components.size() && sorted_components[j].component->component_key ==
                                               sorted_components[i].component->component_key) {
      ++j;
    }
    if (j - i > 1) {
      report.add_error(DiagnosticCode{"schema.duplicate_component_key"},
                       path_components_key(sorted_components[i].component->component_key),
                       "duplicate component key in component module descriptors");
    }
    i = j;
  }

  for (const ComponentRef& ref : sorted_components) {
    const ComponentModuleDescriptor& module = *ref.module;
    const ComponentDescriptor& component = *ref.component;
    const DiagnosticPath comp_path = path_components_key(component.component_key);

    const auto module_it = resolved_module_version.find(std::string{module.module_id});
    if (module_it == resolved_module_version.end()) {
      report.add_error(DiagnosticCode{"schema.unknown_module"}, comp_path,
                       "component references a module that is missing or invalid");
    }

    validate_component_fields(component.component_key, component.fields);
    validate_component_ops(module, component, report);
  }

  if (report.has_errors()) {
    return false;
  }

  std::vector<FrozenComponentRecord> frozen;
  frozen.reserve(sorted_components.size());

  std::unordered_map<uint64_t, std::string> id_to_key;
  id_to_key.reserve(sorted_components.size());

  for (const ComponentRef& ref : sorted_components) {
    const ComponentModuleDescriptor& module = *ref.module;
    const ComponentDescriptor& component = *ref.component;
    const uint64_t sid = stable_component_id_v1(component.component_key);
    const auto [it, inserted] = id_to_key.emplace(sid, std::string{component.component_key});
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

    std::vector<FrozenComponentFieldRecord> fields;
    fields.reserve(component.fields.size());
    for (const ComponentFieldDescriptor& field : component.fields) {
      fields.push_back(freeze_field(field));
    }

    frozen.push_back(FrozenComponentRecord{
        .component_key = std::string{component.component_key},
        .module_id = std::string{module.module_id},
        .module_version = module.module_version,
        .schema_version = component.schema_version,
        .storage = component.storage,
        .visibility = component.visibility,
        .add_on_create = component.add_on_create,
        .schema_validation_hook = component.schema_validation_hook,
        .fields = std::move(fields),
        .ops = component.ops,
        .stable_id = sid,
    });
  }

  for (const FrozenComponentRecord& record : frozen) {
    if (record.schema_validation_hook != nullptr) {
      record.schema_validation_hook(record, report);
    }
  }

  if (report.has_errors()) {
    out = ComponentRegistry{};
    return false;
  }

  std::vector<FrozenModuleRecord> frozen_modules;
  frozen_modules.reserve(resolved_module_version.size());
  for (const auto& [module_id, version] : resolved_module_version) {
    frozen_modules.push_back(FrozenModuleRecord{.module_id = module_id, .version = version});
  }
  std::ranges::sort(frozen_modules, {}, &FrozenModuleRecord::module_id);

  out.modules_ = std::move(frozen_modules);
  out.components_ = std::move(frozen);
  return true;
}

}  // namespace teng::engine::scene
