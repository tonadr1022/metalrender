#include "TestApp.hpp"

#include <GLFW/glfw3.h>

#include <cstddef>
#include <memory>
#include <tracy/Tracy.hpp>
#include <utility>

#include "ResourceManager.hpp"
#include "TestDebugScenes.hpp"
#include "TestRenderer.hpp"
#include "Util.hpp"
#include "core/EAssert.hpp"
#include "engine/Engine.hpp"
#include "engine/ImGuiOverlayLayer.hpp"
#include "engine/render/RenderService.hpp"
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

    auto renderer = std::make_unique<gfx::TestRenderer>(gfx::TestRenderer::CreateInfo{
        .initial_scene = TestDebugScene::MeshletRenderer,
    });
    renderer_ = renderer.get();
    ctx.renderer().set_renderer(std::move(renderer));
    ResourceManager::init(
        ResourceManager::CreateInfo{.model_gpu_mgr = ctx.renderer().model_gpu_mgr()});
    renderer_->set_scene(TestDebugScene::MeshletRenderer);
  }

  void on_detach(teng::engine::EngineContext&) override {
    if (!renderer_) {
      return;
    }
    renderer_->shutdown();
    ResourceManager::shutdown();
    renderer_ = nullptr;
  }

  void on_key_event(teng::engine::EngineContext& ctx, int key, int action, int mods) override {
    if (action == GLFW_PRESS && key >= GLFW_KEY_0 && key <= GLFW_KEY_9 &&
        (mods & GLFW_MOD_CONTROL) != 0) {
      renderer_->apply_demo_scene_preset(static_cast<size_t>(key - GLFW_KEY_0));
    }
    if (key == GLFW_KEY_G && mods & GLFW_MOD_ALT) {
      ctx.toggle_imgui_enabled();
    }
  }

  void on_imgui(teng::engine::EngineContext& ctx) override {
    ZoneScoped;
    ImGui::Begin("TestApp");
    ImGui::Text("ImGui enabled: %d", ctx.imgui_enabled());
    if (renderer_cv::developer_render_graph_dump_mode.get() == 3) {
      ImGui::Separator();
      ImGui::TextWrapped("dump_dir: %s", renderer_cv::developer_render_graph_dump_dir.get());
      if (ImGui::Button("Dump render graph (JSON+DOT)")) {
        ctx.renderer().request_render_graph_debug_dump();
      }
    }
    ImGui::End();
    renderer_->imgui_scene_overlay();
  }

  void on_update(teng::engine::EngineContext& ctx, const teng::engine::EngineTime&) override {
    renderer_->update(ctx.renderer().frame_context());
  }

  void on_render(teng::engine::EngineContext& ctx) override {
    ctx.renderer().enqueue_active_scene();
  }

 private:
  gfx::TestRenderer* renderer_{};
};

}  // namespace

TestApp::TestApp(TestAppOptions options)
    : engine_(std::make_unique<teng::engine::Engine>(teng::engine::EngineConfig{
          .resource_dir = get_resource_dir(),
          .app_name = "vktest",
          .preferred_gfx_api = teng::engine::EngineGfxApi::PlatformDefault,
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
  engine_->layers().push_layer(std::make_unique<teng::engine::ImGuiOverlayLayer>());
}

TestApp::~TestApp() = default;

void TestApp::run() { engine_->run(); }
