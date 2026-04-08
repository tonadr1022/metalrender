#pragma once

#include <memory>

#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/BufferResize.hpp"

namespace teng {

class Window;

namespace gfx {

class ShaderManager;
struct GPUFrameAllocator3;
class ImGuiRenderer;

namespace rhi {

class Device;
class Swapchain;

}  // namespace rhi

struct TestSceneContext {
  rhi::Device* device{};
  rhi::Swapchain* swapchain{};
  teng::Window* window{};
  ShaderManager* shader_mgr{};
  RenderGraph* rg{};
  BufferCopyMgr* buffer_copy{};
  GPUFrameAllocator3* frame_staging{};
  ImGuiRenderer* imgui_renderer{};
  uint32_t curr_frame_idx{};
  float time_sec{};
};

enum class TestDebugScene : uint8_t {
  TexturedCubeProcedural,
  ComputePlusVertexOverlay,
  MeshHelloTriangle,
  MeshletRenderer,
  Count,
};

class ITestScene {
 public:
  virtual ~ITestScene() = default;
  explicit ITestScene(const TestSceneContext& ctx) : ctx_(ctx) {}
  virtual void shutdown() {}
  virtual void on_swapchain_resize() = 0;
  virtual void add_render_graph_passes() = 0;

 protected:
  [[maybe_unused]] const TestSceneContext& ctx_;
};

[[nodiscard]] const char* to_string(TestDebugScene s);
[[nodiscard]] std::unique_ptr<ITestScene> create_test_scene(TestDebugScene s,
                                                            const TestSceneContext& ctx);

}  // namespace gfx

}  // namespace teng
