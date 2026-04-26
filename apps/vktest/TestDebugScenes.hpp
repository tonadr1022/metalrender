#pragma once

#include <filesystem>
#include <memory>

#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/BufferResize.hpp"

namespace teng {

class Window;

namespace engine {
class Scene;
class SceneManager;
}  // namespace engine

namespace gfx {

class ShaderManager;
struct GPUFrameAllocator3;
class ModelGPUMgr;

namespace rhi {

class Device;
class Swapchain;
class CmdEncoder;

}  // namespace rhi

struct TestSceneContext {
  rhi::Device* device{};
  rhi::Swapchain* swapchain{};
  RGResourceId curr_swapchain_rg_id{};
  teng::Window* window{};
  ShaderManager* shader_mgr{};
  RenderGraph* rg{};
  BufferCopyMgr* buffer_copy{};
  GPUFrameAllocator3* frame_staging{};
  ModelGPUMgr* model_gpu_mgr{};
  engine::SceneManager* scene_manager{};
  uint32_t curr_frame_in_flight_idx{};
  std::filesystem::path resource_dir{};
  float time_sec{};
  float delta_time_sec{};
  bool imgui_ui_active{};
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
  virtual void on_frame(const TestSceneContext&) {}
  virtual void on_cursor_pos(double, double) {}
  virtual void on_key_event(int, int, int) {}
  virtual void on_imgui() {}
  virtual void apply_demo_scene_preset(size_t) {}
  virtual void sync_compatibility_ecs_scene(engine::Scene&) {}

 protected:
  [[maybe_unused]] const TestSceneContext& ctx_;
};

[[nodiscard]] const char* to_string(TestDebugScene s);
[[nodiscard]] std::unique_ptr<ITestScene> create_test_scene(TestDebugScene s,
                                                            const TestSceneContext& ctx);

}  // namespace gfx

}  // namespace teng
