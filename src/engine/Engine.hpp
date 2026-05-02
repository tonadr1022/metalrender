#pragma once

#include <cstdint>
#include <filesystem>
#include <glm/vec2.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Result.hpp"
#include "engine/Input.hpp"
#include "engine/scene/SceneManager.hpp"
#include "engine/scene/SceneSerialization.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace teng {

class Window;

namespace gfx::rhi {
class Swapchain;
}

namespace engine {

class RenderService;
namespace assets {
class AssetService;
}

enum class EngineGfxApi {
  PlatformDefault,
  Vulkan,
  Metal,
};

struct EngineConfig {
  std::filesystem::path resource_dir;
  std::string app_name{"metalrender"};
  EngineGfxApi preferred_gfx_api{EngineGfxApi::PlatformDefault};
  int initial_window_width{-1};
  int initial_window_height{-1};
  glm::ivec2 initial_window_position{500, 0};
  bool floating_window{false};
  bool vsync{true};
  bool enable_imgui{true};
  std::optional<uint32_t> quit_after_frames;
};

struct EngineTime {
  double total_seconds{};
  float delta_seconds{};
  uint64_t frame_index{};
};

class EngineContext {
 public:
  [[nodiscard]] Window& window() const { return *window_; }
  [[nodiscard]] gfx::rhi::Device& device() const { return *device_; }
  [[nodiscard]] gfx::rhi::Swapchain& swapchain() const { return *swapchain_; }
  [[nodiscard]] const std::filesystem::path& resource_dir() const { return *resource_dir_; }
  [[nodiscard]] const std::filesystem::path& local_resource_dir() const {
    return *local_resource_dir_;
  }
  [[nodiscard]] assets::AssetService& assets() const { return *assets_; }
  [[nodiscard]] SceneManager& scenes() const { return *scenes_; }
  [[nodiscard]] RenderService& renderer() const { return *renderer_; }
  [[nodiscard]] const EngineTime& time() const { return *time_; }
  [[nodiscard]] const EngineInputSnapshot& input() const { return *input_; }
  [[nodiscard]] bool imgui_enabled() const { return *imgui_enabled_; }
  void set_imgui_enabled(bool enabled) { *imgui_enabled_ = enabled; }
  void toggle_imgui_enabled() { *imgui_enabled_ = !*imgui_enabled_; }

 private:
  friend class Engine;

  Window* window_{};
  gfx::rhi::Device* device_{};
  gfx::rhi::Swapchain* swapchain_{};
  const std::filesystem::path* resource_dir_{};
  const std::filesystem::path* local_resource_dir_{};
  assets::AssetService* assets_{};
  SceneManager* scenes_{};
  RenderService* renderer_{};
  const EngineTime* time_{};
  const EngineInputSnapshot* input_{};
  bool* imgui_enabled_{};
};

class Layer {
 public:
  virtual ~Layer() = default;
  virtual void on_attach([[maybe_unused]] EngineContext& ctx) {}
  virtual void on_detach([[maybe_unused]] EngineContext& ctx) {}
  virtual void on_update([[maybe_unused]] EngineContext& ctx,
                         [[maybe_unused]] const EngineTime& time) {}
  virtual void on_imgui([[maybe_unused]] EngineContext& ctx) {}
  virtual void on_render([[maybe_unused]] EngineContext& ctx) {}
  virtual void on_end_frame([[maybe_unused]] EngineContext& ctx) {}
  virtual void on_key_event([[maybe_unused]] EngineContext& ctx, [[maybe_unused]] int key,
                            [[maybe_unused]] int action, [[maybe_unused]] int mods) {}
  virtual void on_cursor_pos([[maybe_unused]] EngineContext& ctx, [[maybe_unused]] double x,
                             [[maybe_unused]] double y) {}
};

class LayerStack {
 public:
  LayerStack() = default;
  LayerStack(const LayerStack&) = delete;
  LayerStack& operator=(const LayerStack&) = delete;
  ~LayerStack();

  void set_context(EngineContext* ctx);
  void push_layer(std::unique_ptr<Layer> layer);
  void clear();

  void dispatch_key_event(int key, int action, int mods);
  void dispatch_cursor_pos(double x, double y);
  void update(const EngineTime& time);
  void imgui();
  void render();
  void end_frame();

 private:
  EngineContext* ctx_{};
  std::vector<std::unique_ptr<Layer>> layers_;
};

class Engine {
 public:
  explicit Engine(EngineConfig config);
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;
  ~Engine();

  bool tick();
  void run();
  void shutdown();
  [[nodiscard]] Result<SceneLoadResult> load_scene(const std::filesystem::path& scene_path);
  [[nodiscard]] Result<SceneLoadResult> load_project_startup_scene();

  [[nodiscard]] EngineContext& context() { return context_; }
  [[nodiscard]] const EngineContext& context() const { return context_; }
  [[nodiscard]] LayerStack& layers() { return layers_; }
  [[nodiscard]] assets::AssetService& assets() { return *assets_; }
  [[nodiscard]] const assets::AssetService& assets() const { return *assets_; }
  [[nodiscard]] SceneManager& scenes() { return scenes_; }
  [[nodiscard]] const SceneManager& scenes() const { return scenes_; }
  [[nodiscard]] RenderService& renderer() { return *renderer_; }
  [[nodiscard]] const RenderService& renderer() const { return *renderer_; }
  [[nodiscard]] const EngineConfig& config() const { return config_; }

 private:
  struct KeyEvent {
    int key{};
    int action{};
    int mods{};
  };
  struct CursorEvent {
    double x{};
    double y{};
  };

  void init();
  void init_resource_paths();
  [[nodiscard]] gfx::rhi::GfxAPI resolve_gfx_api() const;
  void dispatch_pending_events();
  void refresh_input_snapshot_ui_state();
  void clear_transient_input();

  EngineConfig config_;
  std::filesystem::path resource_dir_;
  std::filesystem::path local_resource_dir_;
  std::unique_ptr<Window> window_;
  std::unique_ptr<gfx::rhi::Device> device_;
  gfx::rhi::SwapchainHandleHolder swapchain_;
  std::unique_ptr<assets::AssetService> assets_;
  SceneManager scenes_;
  std::unique_ptr<RenderService> renderer_;
  EngineContext context_;
  LayerStack layers_;
  EngineTime time_;
  EngineInputSnapshot input_snapshot_;
  glm::vec2 last_cursor_pos_{};
  bool have_cursor_pos_{false};
  bool imgui_enabled_{true};
  bool initialized_{false};
  bool shutting_down_{false};
  bool have_prev_time_{false};
  double prev_time_seconds_{};
  uint32_t completed_frames_{};
  std::vector<KeyEvent> pending_key_events_;
  std::vector<CursorEvent> pending_cursor_events_;
};

}  // namespace engine

}  // namespace teng
