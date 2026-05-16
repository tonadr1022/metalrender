#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "core/Logger.hpp"
#include "engine/scene/ComponentRegistry.hpp"
#include "engine/scene/CoreComponentRegistrar.hpp"
#include "engine/scene/SceneComponentContext.hpp"
#include "engine/scene/SceneCooked.hpp"
#include "engine/scene/SceneSerialization.hpp"
#include "engine/scene/SceneSerializationContext.hpp"

namespace {
using namespace teng;
using namespace teng::engine;

void usage() {
  std::cerr << "usage:\n"
            << "  teng-scene-tool validate <path>\n"
            << "  teng-scene-tool cook <in> <out>\n"
            << "  teng-scene-tool dump <binary> <out>\n";
}

struct SceneTestContexts {
  scene::ComponentRegistry component_registry;
  engine::FlecsComponentContext flecs_components;
  engine::SceneSerializationContext scene_serialization;
};

[[nodiscard]] SceneTestContexts make_scene_test_contexts() {
  std::vector<scene::ComponentModuleDescriptor> component_modules;
  const std::span<const scene::ComponentModuleDescriptor> core_modules = core_component_modules();
  component_modules.insert(component_modules.end(), core_modules.begin(), core_modules.end());
  SceneTestContexts contexts{};
  core::DiagnosticReport report;
  if (!scene::try_freeze_component_registry(component_modules, contexts.component_registry,
                                            report)) {
    LERROR("Failed to freeze component registry: {}", report.to_string());
    std::exit(1);
  }
  if (!make_flecs_component_context(contexts.component_registry, contexts.flecs_components,
                                    report)) {
    LERROR("Failed to freeze scene component context: {}", report.to_string());
    std::exit(1);
  }

  contexts.scene_serialization = make_scene_serialization_context(contexts.component_registry);
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
  } else if (command == "cook" && argc == 4) {
    const SceneTestContexts contexts = make_scene_test_contexts();
    result = teng::engine::cook_scene_file(contexts.scene_serialization, argv[2], argv[3]);
  } else if (command == "dump" && argc == 4) {
    const SceneTestContexts contexts = make_scene_test_contexts();
    result = teng::engine::dump_cooked_scene_file(contexts.scene_serialization, argv[2], argv[3]);
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
