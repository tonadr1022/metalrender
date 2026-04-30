#include <GLFW/glfw3.h>

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
#include <utility>

#include "core/EAssert.hpp"
#include "core/TomlUtil.hpp"
#include "engine/Engine.hpp"
#include "engine/ImGuiOverlayLayer.hpp"
#include "engine/assets/AssetService.hpp"
#include "engine/render/RenderService.hpp"
#include "engine/scene/SceneAssetLoader.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/RendererCVars.hpp"

namespace {

using teng::AlwaysAssert;

struct RuntimeOptions {
  std::filesystem::path scene_path;
  std::optional<std::uint32_t> quit_after_frames;
};

void usage(const char* argv0) {
  std::cout << "usage: " << argv0 << " [--scene <path>] [--quit-after-frames <n>]\n"
            << "  --scene              Load a TOML scene asset instead of project startup_scene\n"
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

std::filesystem::path load_project_startup_scene(const std::filesystem::path& resource_dir) {
  const std::filesystem::path project_path = resource_dir / "project.toml";
  teng::Result<toml::table> project = teng::parse_toml_file(project_path);
  if (!project) {
    std::cerr << "metalrender: failed to load project config " << project_path << ": "
              << project.error() << '\n';
    std::exit(1);
  }

  const std::optional<int64_t> schema_version = (*project)["schema_version"].value<int64_t>();
  if (!schema_version || *schema_version != 1) {
    std::cerr << "metalrender: project config schema_version must be 1\n";
    std::exit(1);
  }

  const std::optional<std::string> startup_scene = (*project)["startup_scene"].value<std::string>();
  if (!startup_scene || startup_scene->empty()) {
    std::cerr << "metalrender: project config is missing startup_scene\n";
    std::exit(1);
  }
  return *startup_scene;
}

class RuntimeSceneLayer final : public teng::engine::Layer {
 public:
  explicit RuntimeSceneLayer(std::filesystem::path scene_path)
      : scene_path_(std::move(scene_path)) {}

  void on_attach(teng::engine::EngineContext& ctx) override {
    ZoneScoped;
    ALWAYS_ASSERT(teng::gfx::RenderGraph::run_barrier_coalesce_self_tests());
    teng::gfx::apply_renderer_cvar_device_constraints(true);
    (void)ctx.assets().scan();

    const std::filesystem::path scene_path =
        scene_path_.empty() ? load_project_startup_scene(ctx.resource_dir()) : scene_path_;
    teng::Result<teng::engine::SceneAssetLoadResult> loaded =
        teng::engine::load_scene_asset(ctx.scenes(), scene_path);
    if (!loaded) {
      std::cerr << "metalrender: failed to load scene " << scene_path << ": " << loaded.error()
                << '\n';
      std::exit(1);
    }
  }

  void on_key_event(teng::engine::EngineContext& ctx, int key, int action, int mods) override {
    if (action == GLFW_PRESS && key == GLFW_KEY_G && (mods & GLFW_MOD_ALT) != 0) {
      ctx.toggle_imgui_enabled();
    }
  }

  void on_imgui(teng::engine::EngineContext& ctx) override { ctx.renderer().on_imgui(); }

  void on_render(teng::engine::EngineContext& ctx) override {
    ctx.renderer().enqueue_active_scene();
  }

 private:
  std::filesystem::path scene_path_;
};

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
  engine.layers().push_layer(std::make_unique<RuntimeSceneLayer>(options->scene_path));
  engine.layers().push_layer(std::make_unique<teng::engine::ImGuiOverlayLayer>());
  engine.run();
  return 0;
}
