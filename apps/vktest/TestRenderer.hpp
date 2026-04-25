#pragma once

#include <filesystem>
#include <memory>

#include "TestDebugScenes.hpp"
#include "engine/render/IRenderer.hpp"
#include "engine/render/RenderFrameContext.hpp"

namespace teng {
class Window;

namespace gfx {

class TestRenderer final : public engine::IRenderer {
 public:
  struct CreateInfo {
    TestDebugScene initial_scene{TestDebugScene::MeshletRenderer};
  };
  explicit TestRenderer(const CreateInfo& cinfo);
  void render(engine::RenderFrameContext& frame, const engine::RenderScene& scene) override;
  void imgui_scene_overlay();
  void on_cursor_pos(double x, double y);
  void on_key_event(int key, int action, int mods);
  void shutdown();
  void on_resize(engine::RenderFrameContext& frame) override;
  void cycle_debug_scene();
  void set_scene(TestDebugScene id);
  void apply_demo_scene_preset(size_t index);
  ~TestRenderer() override;

 private:
  void populate_compatibility_context(engine::RenderFrameContext& frame);
  void add_render_graph_passes();
  void imgui_device_info() const;

  std::unique_ptr<ITestScene> scene_;
  TestDebugScene active_scene_{TestDebugScene::TexturedCubeProcedural};

  TestSceneContext ctx_;
  float prev_time_sec_{};
  bool have_prev_time_{};
};

}  // namespace gfx

}  // namespace teng
