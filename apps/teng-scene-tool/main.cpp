#include <cxxopts.hpp>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

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

[[nodiscard]] constexpr bool is_help_flag(std::string_view s) {
  return s == "-h" || s == "--help";
}

[[nodiscard]] std::string global_help_text(const char* exe) {
  cxxopts::Options global(exe, "GPU-free scene validate / cook / dump");
  // clang-format off
  global.add_options()
    ("h,help", "Show commands and usage");
  // clang-format on
  std::ostringstream out;
  out << "Commands:\n"
      << "  " << exe << " validate <path>\n"
      << "  " << exe << " cook <in> <out>\n"
      << "  " << exe << " dump <binary> <out>\n"
      << '\n'
      << global.help() << '\n';
  return out.str();
}

[[nodiscard]] std::string validate_command_help(const char* exe) {
  cxxopts::Options o(std::string(exe) + " validate", "Validate a *.tscene.json file");
  // clang-format off
  o.add_options()
    ("h,help", "Show this help");
  // clang-format on
  std::ostringstream out;
  out << o.help() << '\n'
      << "Usage: " << exe << " validate <path>\n";
  return out.str();
}

[[nodiscard]] std::string cook_command_help(const char* exe) {
  cxxopts::Options o(std::string(exe) + " cook", "Cook JSON scene to binary *.tscene.bin");
  // clang-format off
  o.add_options()
    ("h,help", "Show this help");
  // clang-format on
  std::ostringstream out;
  out << o.help() << '\n'
      << "Usage: " << exe << " cook <in.json> <out.bin>\n";
  return out.str();
}

[[nodiscard]] std::string dump_command_help(const char* exe) {
  cxxopts::Options o(std::string(exe) + " dump", "Dump a cooked scene binary to JSON");
  // clang-format off
  o.add_options()
    ("h,help", "Show this help");
  // clang-format on
  std::ostringstream out;
  out << o.help() << '\n'
      << "Usage: " << exe << " dump <binary> <out.json>\n";
  return out.str();
}

void usage_error(const char* exe) {
  std::cerr << global_help_text(exe);
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
    usage_error(argv[0]);
    return 1;
  }

  if (argc >= 2 && is_help_flag(argv[1])) {
    std::cout << global_help_text(argv[0]);
    return 0;
  }

  const std::string_view command = argv[1];

  if (command == "validate") {
    if (argc == 3 && is_help_flag(argv[2])) {
      const char* const sub_argv[] = {argv[0], argv[2]};
      cxxopts::Options o(std::string(argv[0]) + " validate", "Validate a *.tscene.json file");
      // clang-format off
      o.add_options()
        ("h,help", "Show this help");
      // clang-format on
      try {
        const auto r = o.parse(2, sub_argv);
        if (r.contains("help")) {
          std::cout << validate_command_help(argv[0]);
          return 0;
        }
      } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << argv[0] << ": " << e.what() << '\n';
        std::cout << validate_command_help(argv[0]);
        return 1;
      }
    }
    if (argc != 3 || is_help_flag(argv[2])) {
      usage_error(argv[0]);
      return 1;
    }
    const SceneTestContexts contexts = make_scene_test_contexts();
    const teng::Result<void> result =
        teng::engine::validate_scene_file(contexts.scene_serialization, argv[2]);
    if (!result) {
      std::cerr << "teng-scene-tool: " << result.error() << '\n';
      return 1;
    }
    return 0;
  }

  if (command == "cook") {
    if (argc == 3 && is_help_flag(argv[2])) {
      const char* const sub_argv[] = {argv[0], argv[2]};
      cxxopts::Options o(std::string(argv[0]) + " cook", "Cook JSON scene to binary");
      // clang-format off
      o.add_options()
        ("h,help", "Show this help");
      // clang-format on
      try {
        const auto r = o.parse(2, sub_argv);
        if (r.contains("help")) {
          std::cout << cook_command_help(argv[0]);
          return 0;
        }
      } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << argv[0] << ": " << e.what() << '\n';
        std::cout << cook_command_help(argv[0]);
        return 1;
      }
    }
    if (argc != 4 || is_help_flag(argv[2]) || is_help_flag(argv[3])) {
      usage_error(argv[0]);
      return 1;
    }
    const SceneTestContexts contexts = make_scene_test_contexts();
    const teng::Result<void> result =
        teng::engine::cook_scene_file(contexts.scene_serialization, argv[2], argv[3]);
    if (!result) {
      std::cerr << "teng-scene-tool: " << result.error() << '\n';
      return 1;
    }
    return 0;
  }

  if (command == "dump") {
    if (argc == 3 && is_help_flag(argv[2])) {
      const char* const sub_argv[] = {argv[0], argv[2]};
      cxxopts::Options o(std::string(argv[0]) + " dump", "Dump cooked scene to JSON");
      // clang-format off
      o.add_options()
        ("h,help", "Show this help");
      // clang-format on
      try {
        const auto r = o.parse(2, sub_argv);
        if (r.contains("help")) {
          std::cout << dump_command_help(argv[0]);
          return 0;
        }
      } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << argv[0] << ": " << e.what() << '\n';
        std::cout << dump_command_help(argv[0]);
        return 1;
      }
    }
    if (argc != 4 || is_help_flag(argv[2]) || is_help_flag(argv[3])) {
      usage_error(argv[0]);
      return 1;
    }
    const SceneTestContexts contexts = make_scene_test_contexts();
    const teng::Result<void> result =
        teng::engine::dump_cooked_scene_file(contexts.scene_serialization, argv[2], argv[3]);
    if (!result) {
      std::cerr << "teng-scene-tool: " << result.error() << '\n';
      return 1;
    }
    return 0;
  }

  usage_error(argv[0]);
  return 1;
}
