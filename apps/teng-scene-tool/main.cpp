#include <filesystem>
#include <iostream>
#include <string_view>

#include "engine/scene/SceneSerialization.hpp"

namespace {

void usage() {
  std::cerr << "usage:\n"
            << "  teng-scene-tool validate <path>\n"
            << "  teng-scene-tool migrate <in> <out>\n"
            << "  teng-scene-tool cook <in> <out>\n"
            << "  teng-scene-tool dump <binary> <out>\n";
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
    result = teng::engine::validate_scene_file(argv[2]);
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
