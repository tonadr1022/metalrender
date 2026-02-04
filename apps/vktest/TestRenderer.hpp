#pragma once

#include <filesystem>
#include <memory>

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
  std::unique_ptr<gfx::ShaderManager> shader_mgr_;
  rhi::Device* device_;
  rhi::Swapchain* swapchain_;
  rhi::PipelineHandleHolder clear_color_cmp_pso_;
  rhi::PipelineHandleHolder test_gfx_pso_;
};

}  // namespace teng
