#pragma once

#include <filesystem>
#include <memory>

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
};

}  // namespace teng
