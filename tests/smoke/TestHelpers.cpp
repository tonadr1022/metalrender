#include "TestHelpers.hpp"

#include "core/Diagnostic.hpp"
#include "core/Logger.hpp"
#include "engine/scene/BuiltinComponentSerialization.hpp"
#include "engine/scene/CoreComponentRegistrar.hpp"

namespace teng::engine {

[[nodiscard]] SceneTestContexts make_scene_test_contexts() {
  core::ComponentRegistryBuilder component_registry_builder;
  register_core_components(component_registry_builder);
  auto component_registry = std::make_unique<core::ComponentRegistry>();
  core::DiagnosticReport report;
  if (!component_registry_builder.try_freeze(*component_registry, report)) {
    LERROR("Failed to freeze component registry: {}", report.to_string());
    std::exit(1);
  }
  FlecsComponentContextBuilder flecs_builder{*component_registry};
  register_flecs_core_components(flecs_builder);
  FlecsComponentContext component_ctx;
  if (!flecs_builder.try_freeze(component_ctx, report)) {
    LERROR("Failed to freeze scene component context: {}", report.to_string());
    std::exit(1);
  }

  SceneSerializationContextBuilder serialization_builder{*component_registry};
  register_builtin_component_serialization(serialization_builder);
  return SceneTestContexts{.component_registry = std::move(component_registry),
                           .flecs_components = std::move(component_ctx),
                           .scene_serialization = serialization_builder.freeze()};
}

[[nodiscard]] FlecsComponentContext make_scene_component_context() {
  SceneTestContexts contexts = make_scene_test_contexts();
  return std::move(contexts.flecs_components);
}

}  // namespace teng::engine
