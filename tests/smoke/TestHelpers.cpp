#include "TestHelpers.hpp"

#include "core/Diagnostic.hpp"
#include "core/Logger.hpp"
#include "engine/scene/CoreComponentRegistrar.hpp"

namespace teng::engine {

[[nodiscard]] FlecsComponentContext make_scene_component_context() {
  core::ComponentRegistryBuilder component_registry_builder;
  register_core_components(component_registry_builder);
  core::ComponentRegistry component_registry;
  core::DiagnosticReport report;
  if (!component_registry_builder.try_freeze(component_registry, report)) {
    LERROR("Failed to freeze component registry: {}", report.to_string());
    std::exit(1);
  }
  FlecsComponentContextBuilder builder{component_registry};
  register_flecs_core_components(builder);
  FlecsComponentContext component_ctx;
  if (!builder.try_freeze(component_ctx, report)) {
    LERROR("Failed to freeze scene component context: {}", report.to_string());
    std::exit(1);
  }
  return component_ctx;
}

}  // namespace teng::engine