#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "engine/render/RenderFrameContext.hpp"
#include "engine/render/RenderScene.hpp"
#include "gfx/BackedGPUAllocator.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/BufferResize.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace teng {
class Window;

namespace gfx {
class ImGuiRenderer;
class ModelGPUMgr;
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
  void begin_frame();
  void enqueue_active_scene();
  void enqueue_imgui_overlay_pass();
  void shutdown_imgui_renderer();
  void end_frame();
  void render_scene(const RenderScene& scene);
  void set_imgui_ui_active(bool active);
  void request_render_graph_debug_dump();
  void recreate_resources_on_swapchain_resize();

  [[nodiscard]] RenderFrameContext& frame_context() { return frame_; }
  [[nodiscard]] const RenderFrameContext& frame_context() const { return frame_; }
  [[nodiscard]] const RenderScene& last_extracted_scene() const { return last_extracted_scene_; }
  [[nodiscard]] gfx::RenderGraph& render_graph() { return render_graph_; }
  [[nodiscard]] gfx::ModelGPUMgr* model_gpu_mgr() const { return model_gpu_mgr_.get(); }

 private:
  void update_frame_context();
  void flush_pending_buffer_copies(gfx::rhi::CmdEncoder* enc);
  void flush_pending_texture_uploads(gfx::rhi::CmdEncoder* enc);

  gfx::rhi::Device* device_{};
  gfx::rhi::Swapchain* swapchain_{};
  Window* window_{};
  SceneManager* scenes_{};
  const EngineTime* time_{};
  std::filesystem::path resource_dir_;
  std::unique_ptr<gfx::ShaderManager> shader_mgr_;
  std::unique_ptr<gfx::GPUFrameAllocator3> frame_gpu_upload_allocator_;
  std::unique_ptr<gfx::BufferCopyMgr> buffer_copy_mgr_;
  std::unique_ptr<gfx::ImGuiRenderer> imgui_renderer_;
  std::unique_ptr<gfx::InstanceMgr> static_instance_mgr_;
  std::unique_ptr<gfx::GeometryBatch> static_draw_batch_;
  std::unique_ptr<gfx::BackedGPUAllocator> materials_buf_;
  std::unique_ptr<gfx::ModelGPUMgr> model_gpu_mgr_;
  std::unique_ptr<IRenderer> renderer_;
  gfx::RenderGraph render_graph_;
  RenderFrameContext frame_;
  RenderScene last_extracted_scene_;
  std::vector<gfx::rhi::SamplerHandleHolder> samplers_;
  bool frame_open_{};
  bool initialized_{};
};

}  // namespace engine
}  // namespace teng
