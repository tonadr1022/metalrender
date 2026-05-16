#include <cstdint>
#include <cxxopts.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <tracy/Tracy.hpp>

#include "editor/EditorLayer.hpp"
#include "engine/Engine.hpp"
#include "engine/ImGuiOverlayLayer.hpp"

int main(int argc, char* argv[]) {
  ZoneScoped;

  cxxopts::Options options(argv[0], "Teng editor");
  // clang-format off
  options.add_options()
    ("scene", "Open `*.tscene.json` or cooked `*.tscene.bin` instead of project startup_scene",
     cxxopts::value<std::string>())
    ("quit-after-frames", "Exit after completing n frames (n >= 1)",
     cxxopts::value<std::uint32_t>())
    ("h,help", "Show this help");
  // clang-format on

  cxxopts::ParseResult result;
  try {
    result = options.parse(argc, argv);
  } catch (const cxxopts::exceptions::exception& e) {
    std::cerr << argv[0] << ": " << e.what() << '\n';
    std::cout << options.help() << '\n';
    return 1;
  }

  if (result.contains("help")) {
    std::cout << options.help() << '\n';
    return 0;
  }

  std::filesystem::path scene_path;
  if (result.contains("scene")) {
    scene_path = result["scene"].as<std::string>();
  }

  std::optional<std::uint32_t> quit_after_frames;
  if (result.contains("quit-after-frames")) {
    const std::uint32_t frame_count = result["quit-after-frames"].as<std::uint32_t>();
    if (frame_count == 0) {
      std::cerr << argv[0] << ": --quit-after-frames requires a positive 32-bit integer value\n";
      std::cout << options.help() << '\n';
      return 1;
    }
    quit_after_frames = frame_count;
  }

  teng::engine::Engine engine(teng::engine::EngineConfig{
      .resource_dir = {},
      .app_name = "teng_editor",
      .preferred_gfx_api = teng::engine::EngineGfxApi::PlatformDefault,
      .initial_window_width = -1,
      .initial_window_height = -1,
      .initial_window_position = {500, 0},
      .floating_window = false,
      .vsync = true,
      .enable_imgui = true,
      .quit_after_frames = quit_after_frames,
  });
  teng::Result<teng::engine::SceneLoadResult> loaded =
      scene_path.empty() ? engine.load_project_startup_scene() : engine.load_scene(scene_path);
  if (!loaded) {
    std::cerr << "teng_editor: failed to load scene: " << loaded.error() << '\n';
    return 1;
  }
  engine.layers().push_layer(std::make_unique<teng::engine::ImGuiOverlayLayer>());
  engine.layers().push_layer(std::make_unique<teng::editor::EditorLayer>());
  engine.run();
  return 0;
}
