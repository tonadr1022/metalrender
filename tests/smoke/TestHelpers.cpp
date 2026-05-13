#include "TestHelpers.hpp"

#include <span>
#include <vector>

#include "TestExtensionComponent.hpp"
#include "core/Diagnostic.hpp"
#include "core/Logger.hpp"
#include "engine/scene/CoreComponentRegistrar.hpp"

namespace teng::engine {

namespace {

void append_modules(std::vector<scene::ComponentModuleDescriptor>& out,
                    std::span<const scene::ComponentModuleDescriptor> modules) {
  out.insert(out.end(), modules.begin(), modules.end());
}

[[nodiscard]] SceneTestContexts make_contexts_from_modules(
    const std::vector<scene::ComponentModuleDescriptor>& component_modules) {
  auto component_registry = std::make_unique<scene::ComponentRegistry>();
  core::DiagnosticReport report;
  if (!scene::try_freeze_component_registry(component_modules, *component_registry, report)) {
    LERROR("Failed to freeze component registry: {}", report.to_string());
    std::exit(1);
  }

  FlecsComponentContext component_ctx;
  if (!make_flecs_component_context(*component_registry, component_ctx, report)) {
    LERROR("Failed to freeze scene component context: {}", report.to_string());
    std::exit(1);
  }

  const SceneSerializationContext serialization =
      make_scene_serialization_context(*component_registry);
  return SceneTestContexts{
      .component_registry = std::move(component_registry),
      .flecs_components = std::move(component_ctx),
      .scene_serialization = serialization,
  };
}

}  // namespace

[[nodiscard]] SceneTestContexts make_scene_test_contexts() {
  std::vector<scene::ComponentModuleDescriptor> component_modules;
  append_modules(component_modules, core_component_modules());
  return make_contexts_from_modules(component_modules);
}

[[nodiscard]] SceneTestContexts make_scene_test_contexts_with_test_extension() {
  std::vector<scene::ComponentModuleDescriptor> component_modules;
  append_modules(component_modules, core_component_modules());
  append_modules(component_modules, test_extension_component_modules());
  return make_contexts_from_modules(component_modules);
}

[[nodiscard]] FlecsComponentContext make_scene_component_context() {
  SceneTestContexts contexts = make_scene_test_contexts();
  return std::move(contexts.flecs_components);
}

[[nodiscard]] SceneSerializationContext make_scene_serialization_context() {
  const SceneTestContexts contexts = make_scene_test_contexts();
  return contexts.scene_serialization;
}
}  // namespace teng::engine
