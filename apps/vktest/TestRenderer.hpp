#pragma once

#include <filesystem>
#include <memory>

#include "TestDebugScenes.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/BufferResize.hpp"
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
    TestDebugScene initial_scene{TestDebugScene::TexturedCubeProcedural};
  };
  explicit TestRenderer(const CreateInfo& cinfo);
  void render();
  void recreate_resources_on_swapchain_resize();
  void cycle_debug_scene();
  void set_scene(TestDebugScene id);
  ~TestRenderer();

 private:
  void add_render_graph_passes();
  void init_imgui();
  void update_ctx();

  std::unique_ptr<ITestScene> scene_;
  TestDebugScene active_scene_{TestDebugScene::TexturedCubeProcedural};

  TestSceneContext ctx_;
  std::unique_ptr<gfx::ShaderManager> shader_mgr_;
  rhi::Device* device_;
  rhi::Swapchain* swapchain_;
  GPUFrameAllocator3 frame_gpu_upload_allocator_;
  std::unique_ptr<ImGuiRenderer> imgui_renderer_;
  std::filesystem::path resource_dir_;
  BufferCopyMgr buffer_copy_mgr_;
  RenderGraph rg_;
  Window* window_{};
};

}  // namespace gfx

}  // namespace teng
