#include "TestHelpers.hpp"

#include "core/Diagnostic.hpp"
#include "core/Logger.hpp"
#include "engine/scene/CoreComponentRegistrar.hpp"

namespace teng::engine {

[[nodiscard]] SceneComponentContext make_scene_component_context() {
  SceneComponentContextBuilder builder;
  register_core_scene_component_bindings(builder);
  SceneComponentContext component_ctx;
  core::DiagnosticReport report;
  if (!builder.try_freeze(component_ctx, report)) {
    LERROR("Failed to freeze scene component context: {}", report.to_string());
    std::exit(1);
  }
  return component_ctx;
}

}  // namespace teng::engine