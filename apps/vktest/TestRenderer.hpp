#pragma once

#include <filesystem>
#include <memory>

#include "TestDebugScenes.hpp"
#include "gfx/BackedGPUAllocator.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/BufferResize.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace teng {
class Window;

namespace gfx {
class ShaderManager;

namespace rhi {

class CmdEncoder;
class Device;
class Swapchain;

}  // namespace rhi

}  // namespace gfx

namespace gfx {

class TestRenderer {
 public:
  struct CreateInfo {
    rhi::Device* device;
    rhi::Swapchain* swapchain;
    Window* window;
    std::filesystem::path resource_dir;
    TestDebugScene initial_scene{TestDebugScene::MeshletRenderer};
  };
  explicit TestRenderer(const CreateInfo& cinfo);
  void render(bool imgui_ui_active);
  void imgui_scene_overlay();
  void on_cursor_pos(double x, double y);
  void on_key_event(int key, int action, int mods);
  void shutdown();
  void recreate_resources_on_swapchain_resize();
  void cycle_debug_scene();
  void set_scene(TestDebugScene id);
  ModelGPUMgr* get_model_gpu_mgr() { return model_gpu_mgr_.get(); }
  ~TestRenderer();

 private:
  void add_render_graph_passes();
  void init_imgui();
  void update_ctx();

  std::unique_ptr<ITestScene> scene_;
  TestDebugScene active_scene_{TestDebugScene::TexturedCubeProcedural};

  TestSceneContext ctx_;
  std::unique_ptr<gfx::ShaderManager> shader_mgr_;
  std::unique_ptr<ModelGPUMgr> model_gpu_mgr_;
  rhi::Device* device_;
  rhi::Swapchain* swapchain_;
  GPUFrameAllocator3 frame_gpu_upload_allocator_;
  std::unique_ptr<ImGuiRenderer> imgui_renderer_;
  std::filesystem::path resource_dir_;
  BufferCopyMgr buffer_copy_mgr_;
  RenderGraph rg_;
  Window* window_{};
  InstanceMgr static_instance_mgr_;
  GeometryBatch static_draw_batch_;
  BackedGPUAllocator materials_buf_;
  float prev_time_sec_{};
  bool have_prev_time_{};
};

}  // namespace gfx

}  // namespace teng
