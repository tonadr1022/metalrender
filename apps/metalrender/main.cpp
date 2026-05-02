#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tracy/Tracy.hpp>

#include "engine/Engine.hpp"
#include "engine/ImGuiOverlayLayer.hpp"

namespace {

struct RuntimeOptions {
  std::filesystem::path scene_path;
  std::optional<std::uint32_t> quit_after_frames;
};

void usage(const char* argv0) {
  std::cout << "usage: " << argv0 << " [--scene <path>] [--quit-after-frames <n>]\n"
            << "  --scene              Load a canonical JSON scene instead of project startup_scene\n"
            << "  --quit-after-frames  Exit after completing n frames (n >= 1)\n"
            << "  -h, --help           Show this help\n";
}

bool parse_u32(std::string_view text, std::uint32_t& out) {
  if (text.empty()) {
    return false;
  }
  std::uint32_t value{};
  const char* const begin = text.data();
  const char* const end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, value, 10);
  if (result.ptr == begin || result.ptr != end || result.ec != std::errc{} || value == 0) {
    return false;
  }
  out = value;
  return true;
}

std::optional<RuntimeOptions> parse_options(int argc, char* argv[]) {
  RuntimeOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--scene" && i + 1 < argc) {
      options.scene_path = argv[++i];
    } else if (arg == "--quit-after-frames" && i + 1 < argc) {
      std::uint32_t frame_count{};
      if (!parse_u32(std::string_view(argv[++i]), frame_count)) {
        std::cerr << argv[0] << ": --quit-after-frames requires a positive 32-bit integer value\n";
        return std::nullopt;
      }
      options.quit_after_frames = frame_count;
    } else if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      std::exit(0);
    } else {
      std::cerr << argv[0] << ": unknown option: " << arg << '\n';
      usage(argv[0]);
      return std::nullopt;
    }
  }

  return options;
}

}  // namespace

int main(int argc, char* argv[]) {
  ZoneScoped;
  std::optional<RuntimeOptions> options = parse_options(argc, argv);
  if (!options) {
    return 1;
  }

  teng::engine::Engine engine(teng::engine::EngineConfig{
      .resource_dir = {},
      .app_name = "metalrender",
      .preferred_gfx_api = teng::engine::EngineGfxApi::PlatformDefault,
      .initial_window_width = -1,
      .initial_window_height = -1,
      .initial_window_position = {500, 0},
      .floating_window = false,
      .vsync = true,
      .enable_imgui = true,
      .quit_after_frames = options->quit_after_frames,
  });
  teng::Result<teng::engine::SceneLoadResult> loaded =
      options->scene_path.empty() ? engine.load_project_startup_scene()
                                  : engine.load_scene(options->scene_path);
  if (!loaded) {
    std::cerr << "metalrender: failed to load scene: " << loaded.error() << '\n';
    return 1;
  }
  engine.layers().push_layer(std::make_unique<teng::engine::ImGuiOverlayLayer>());
  engine.run();
  return 0;
}
