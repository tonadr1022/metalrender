#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "engine/render/RenderFrameContext.hpp"
#include "engine/render/RenderScene.hpp"
#include "gfx/RenderGraph.hpp"

namespace teng {
class Window;

namespace gfx {
class ShaderManager;

namespace rhi {
class Device;
class Swapchain;
}  // namespace rhi
}  // namespace gfx

namespace engine {

class IRenderer;
class SceneManager;
struct EngineTime;

class RenderService {
 public:
  struct CreateInfo {
    gfx::rhi::Device* device{};
    gfx::rhi::Swapchain* swapchain{};
    Window* window{};
    SceneManager* scenes{};
    const EngineTime* time{};
    std::filesystem::path resource_dir;
    bool imgui_ui_active{};
  };

  RenderService() = default;
  explicit RenderService(const CreateInfo& cinfo);
  RenderService(const RenderService&) = delete;
  RenderService& operator=(const RenderService&) = delete;
  ~RenderService();

  void init(const CreateInfo& cinfo);
  void shutdown();
  void set_renderer(std::unique_ptr<IRenderer> renderer);
  void render_active_scene();
  void render_scene(const RenderScene& scene);
  void request_render_graph_debug_dump();
  void recreate_resources_on_swapchain_resize();

  [[nodiscard]] RenderFrameContext& frame_context() { return frame_; }
  [[nodiscard]] const RenderFrameContext& frame_context() const { return frame_; }
  [[nodiscard]] const RenderScene& last_extracted_scene() const { return last_extracted_scene_; }
  [[nodiscard]] gfx::RenderGraph& render_graph() { return render_graph_; }

 private:
  void update_frame_context();
  void execute_graph();

  gfx::rhi::Device* device_{};
  gfx::rhi::Swapchain* swapchain_{};
  Window* window_{};
  SceneManager* scenes_{};
  const EngineTime* time_{};
  std::filesystem::path resource_dir_;
  std::unique_ptr<gfx::ShaderManager> shader_mgr_;
  std::unique_ptr<IRenderer> renderer_;
  gfx::RenderGraph render_graph_;
  RenderFrameContext frame_;
  RenderScene last_extracted_scene_;
  bool initialized_{};
};

}  // namespace engine
}  // namespace teng
