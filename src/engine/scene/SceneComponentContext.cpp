#include "engine/scene/SceneComponentContext.hpp"

#include <unordered_map>

#include "engine/scene/ComponentRegistry.hpp"

namespace TENG_NAMESPACE::engine {

namespace {

[[nodiscard]] core::DiagnosticPath path_components_key(std::string_view component_key) {
  core::DiagnosticPath path;
  path.object_key("components").object_key(std::string{component_key});
  return path;
}

}  // namespace

void FlecsComponentContextBuilder::register_flecs_component(
    FlecsComponentBinding flecs_component_binding) {
  flecs_component_bindings_.push_back(flecs_component_binding);
}

bool FlecsComponentContextBuilder::try_freeze(FlecsComponentContext& out,
                                              core::DiagnosticReport& report) const {
  std::unordered_map<std::string, FlecsComponentBinding> component_key_to_flecs_binding;
  for (const FlecsComponentBinding& binding : flecs_component_bindings_) {
    component_key_to_flecs_binding.emplace(binding.component_key, binding);
  }

  bool ok = true;
  for (const scene::FrozenComponentRecord& record : registry_.components()) {
    if (record.storage == scene::ComponentStoragePolicy::EditorOnly) {
      continue;
    }

    const auto it = component_key_to_flecs_binding.find(record.component_key);
    if (it == component_key_to_flecs_binding.end()) {
      report.add_error(core::DiagnosticCode{"schema.missing_flecs_binding"},
                       path_components_key(record.component_key),
                       "non-EditorOnly component is missing Flecs binding");
      ok = false;
      continue;
    }

    const FlecsComponentBinding& binding = it->second;
    if (binding.register_flecs_fn == nullptr) {
      report.add_error(core::DiagnosticCode{"schema.missing_register_flecs_fn"},
                       path_components_key(record.component_key), "register_flecs_fn is required");
      ok = false;
    }

    if (record.add_on_create && binding.apply_on_create_fn == nullptr) {
      report.add_error(core::DiagnosticCode{"schema.missing_apply_on_create_fn"},
                       path_components_key(record.component_key),
                       "apply_on_create_fn is required for add_on_create components");
      ok = false;
    }
  }

  if (!ok) {
    out = FlecsComponentContext{};
    return false;
  }

  std::vector<ApplyOnCreateFn> apply_on_create_fns;
  std::vector<RegisterFlecsFn> flecs_register_fns;
  apply_on_create_fns.reserve(registry_.components().size());
  flecs_register_fns.reserve(registry_.components().size());

  for (const scene::FrozenComponentRecord& record : registry_.components()) {
    if (record.storage == scene::ComponentStoragePolicy::EditorOnly) {
      continue;
    }
    const FlecsComponentBinding& binding =
        component_key_to_flecs_binding.find(record.component_key)->second;
    if (record.add_on_create) {
      apply_on_create_fns.push_back(binding.apply_on_create_fn);
    }
    flecs_register_fns.push_back(binding.register_flecs_fn);
  }

  out.apply_on_create_fns = std::move(apply_on_create_fns);
  out.flecs_register_fns = std::move(flecs_register_fns);
  return true;
}

}  // namespace TENG_NAMESPACE::engine
