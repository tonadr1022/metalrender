#pragma once

#include <filesystem>
#include <memory>

#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/renderer/BufferResize.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace teng {
namespace gfx {
class ShaderManager;
}
class Window;

namespace rhi {
class CmdEncoder;
class Device;
class Swapchain;

}  // namespace rhi

namespace gfx {

class TestRenderer {
 public:
  struct CreateInfo {
    rhi::Device* device;
    rhi::Swapchain* swapchain;
    Window* window;
    std::filesystem::path resource_dir;
  };
  explicit TestRenderer(const CreateInfo& cinfo);
  void render();
  ~TestRenderer();

 private:
  void recreate_resources_on_swapchain_resize();

  std::unique_ptr<gfx::ShaderManager> shader_mgr_;
  rhi::Device* device_;
  rhi::Swapchain* swapchain_;
  rhi::PipelineHandleHolder clear_color_cmp_pso_;
  rhi::PipelineHandleHolder test_gfx_pso_;
  rhi::PipelineHandleHolder test_geo_pso_;
  rhi::TextureHandleHolder test_full_screen_tex_;
  rhi::TextureHandleHolder white_tex_;
  GPUFrameAllocator3 frame_gpu_upload_allocator_;
  BufferCopyMgr buffer_copy_mgr_;
  rhi::BufferHandleHolder test_vert_buf_;
};

}  // namespace gfx

}  // namespace teng
