#include <filesystem>
#include <iostream>
#include <string_view>

#include "core/ComponentRegistry.hpp"
#include "core/Logger.hpp"
#include "engine/scene/BuiltinComponentSerialization.hpp"
#include "engine/scene/CoreComponentRegistrar.hpp"
#include "engine/scene/SceneComponentContext.hpp"
#include "engine/scene/SceneSerialization.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace {
using namespace teng;
using namespace teng::engine;

void usage() {
  std::cerr << "usage:\n"
            << "  teng-scene-tool validate <path>\n"
            << "  teng-scene-tool migrate <in> <out>\n"
            << "  teng-scene-tool cook <in> <out>\n"
            << "  teng-scene-tool dump <binary> <out>\n";
}

struct SceneTestContexts {
  core::ComponentRegistry component_registry;
  engine::FlecsComponentContext flecs_components;
  engine::SceneSerializationContext scene_serialization;
};

[[nodiscard]] SceneTestContexts make_scene_test_contexts() {
  core::ComponentRegistryBuilder component_registry_builder;
  register_core_components(component_registry_builder);
  SceneTestContexts contexts{};
  core::DiagnosticReport report;
  if (!component_registry_builder.try_freeze(contexts.component_registry, report)) {
    LERROR("Failed to freeze component registry: {}", report.to_string());
    std::exit(1);
  }
  FlecsComponentContextBuilder flecs_builder{contexts.component_registry};
  register_flecs_core_components(flecs_builder);
  if (!flecs_builder.try_freeze(contexts.flecs_components, report)) {
    LERROR("Failed to freeze scene component context: {}", report.to_string());
    std::exit(1);
  }

  SceneSerializationContextBuilder serialization_builder{contexts.component_registry};
  register_builtin_component_serialization(serialization_builder);
  contexts.scene_serialization = serialization_builder.freeze();
  return contexts;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 1;
  }
  const std::string_view command = argv[1];
  teng::Result<void> result;
  if (command == "validate" && argc == 3) {
    const SceneTestContexts contexts = make_scene_test_contexts();
    result = teng::engine::validate_scene_file(contexts.scene_serialization, argv[2]);
  } else if (command == "migrate" && argc == 4) {
    result = teng::engine::migrate_scene_file(argv[2], argv[3]);
  } else if (command == "cook" && argc == 4) {
    result = teng::engine::cook_scene_file(argv[2], argv[3]);
  } else if (command == "dump" && argc == 4) {
    result = teng::engine::dump_cooked_scene_file(argv[2], argv[3]);
  } else {
    usage();
    return 1;
  }
  if (!result) {
    std::cerr << "teng-scene-tool: " << result.error() << '\n';
    return 1;
  }
  return 0;
}
