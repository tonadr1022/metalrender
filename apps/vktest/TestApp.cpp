#include "TestApp.hpp"

#include <GLFW/glfw3.h>

#include <memory>
#include <tracy/Tracy.hpp>

#include "ResourceManager.hpp"
#include "TestRenderer.hpp"
#include "Util.hpp"
#include "core/EAssert.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/RendererCVars.hpp"
#include "imgui.h"

using namespace teng;
using namespace teng::gfx;

namespace {

class CompatibilityVktestLayer final : public teng::engine::Layer {
 public:
  void on_attach(teng::engine::EngineContext& ctx) override {
    ZoneScoped;
    ALWAYS_ASSERT(RenderGraph::run_barrier_coalesce_self_tests());
    gfx::apply_renderer_cvar_device_constraints(true);

    renderer_ = std::make_unique<gfx::TestRenderer>(gfx::TestRenderer::CreateInfo{
        .device = &ctx.device(),
        .swapchain = &ctx.swapchain(),
        .window = &ctx.window(),
        .resource_dir = ctx.resource_dir(),
        .initial_scene = TestDebugScene::MeshletRenderer,
    });
    ResourceManager::init(
        ResourceManager::CreateInfo{.model_gpu_mgr = renderer_->get_model_gpu_mgr()});
    renderer_->set_scene(TestDebugScene::MeshletRenderer);
  }

  void on_detach(teng::engine::EngineContext&) override {
    if (!renderer_) {
      return;
    }
    renderer_->shutdown();
    ResourceManager::shutdown();
    renderer_.reset();
  }

  void on_key_event(teng::engine::EngineContext& ctx, int key, int action, int mods) override {
    if (action == GLFW_PRESS && key == GLFW_KEY_TAB) {
      renderer_->cycle_debug_scene();
    }
    if (action == GLFW_PRESS && key >= GLFW_KEY_0 && key <= GLFW_KEY_9 &&
        (mods & GLFW_MOD_CONTROL) != 0) {
      renderer_->apply_demo_scene_preset(static_cast<size_t>(key - GLFW_KEY_0));
    }
    if (key == GLFW_KEY_G && mods & GLFW_MOD_ALT) {
      ctx.toggle_imgui_enabled();
    }
    renderer_->on_key_event(key, action, mods);
  }

  void on_cursor_pos(teng::engine::EngineContext&, double x, double y) override {
    renderer_->on_cursor_pos(x, y);
  }

  void on_imgui(teng::engine::EngineContext& ctx) override {
    ZoneScoped;
    ImGui::Begin("TestApp");
    ImGui::Text("ImGui enabled: %d", ctx.imgui_enabled());
    if (renderer_cv::developer_render_graph_dump_mode.get() == 3) {
      ImGui::Separator();
      ImGui::TextWrapped("dump_dir: %s", renderer_cv::developer_render_graph_dump_dir.get());
      if (ImGui::Button("Dump render graph (JSON+DOT)")) {
        renderer_->request_render_graph_debug_dump();
      }
    }
    ImGui::End();
    renderer_->imgui_scene_overlay();
  }

  void on_render(teng::engine::EngineContext& ctx) override {
    renderer_->render(ctx.imgui_enabled());
  }

 private:
  std::unique_ptr<gfx::TestRenderer> renderer_;
};

}  // namespace

TestApp::TestApp(TestAppOptions options)
    : engine_(std::make_unique<teng::engine::Engine>(teng::engine::EngineConfig{
          .resource_dir = get_resource_dir(),
          .app_name = "vktest",
          .preferred_gfx_api = teng::engine::EngineGfxApi::Vulkan,
          .initial_window_width = -1,
          .initial_window_height = -1,
          .initial_window_position = {500, 0},
          .floating_window = false,
          .vsync = true,
          .enable_imgui = true,
          .quit_after_frames = options.quit_after_frames,
      })) {
  ZoneScoped;
  engine_->layers().push_layer(std::make_unique<CompatibilityVktestLayer>());
}

TestApp::~TestApp() = default;

void TestApp::run() { engine_->run(); }
