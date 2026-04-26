#include "TestApp.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <memory>
#include <tracy/Tracy.hpp>
#include <utility>
#include <vector>

#include "DemoSceneEcsBridge.hpp"
#include "ResourceManager.hpp"
#include "ScenePresets.hpp"
#include "Util.hpp"
#include "core/EAssert.hpp"
#include "engine/Engine.hpp"
#include "engine/ImGuiOverlayLayer.hpp"
#include "engine/render/RenderService.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneManager.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/MeshletRenderer.hpp"
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

    ResourceManager::init(
        ResourceManager::CreateInfo{.model_gpu_mgr = ctx.renderer().model_gpu_mgr()});
    teng::demo_scenes::seed_demo_scene_rng(10000000);
    load_scene_presets(ctx);
    apply_demo_scene_preset(ctx, 0);
  }

  void on_detach(teng::engine::EngineContext& ctx) override {
    demo_scene_compat::clear_loaded_models(ctx.scenes());
    ResourceManager::shutdown();
  }

  void on_key_event(teng::engine::EngineContext& ctx, int key, int action, int mods) override {
    if (action == GLFW_PRESS && key >= GLFW_KEY_0 && key <= GLFW_KEY_9 &&
        (mods & GLFW_MOD_CONTROL) != 0) {
      apply_demo_scene_preset(ctx, static_cast<size_t>(key - GLFW_KEY_0));
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
    imgui_meshlet_renderer(ctx);
  }

  void on_update(teng::engine::EngineContext& ctx, const teng::engine::EngineTime&) override {
    if (teng::engine::Scene* scene = ctx.scenes().active_scene()) {
      demo_scene_compat::sync_loaded_model_transforms(*scene);
    }
  }

  void on_render(teng::engine::EngineContext& ctx) override {
    ctx.renderer().enqueue_active_scene();
  }

 private:
  void load_scene_presets(teng::engine::EngineContext& ctx) {
    scene_presets_.clear();
    teng::demo_scenes::append_default_scene_preset_data(scene_presets_, ctx.resource_dir());
  }

  void apply_demo_scene_preset(teng::engine::EngineContext& ctx, size_t index) {
    if (scene_presets_.empty()) {
      return;
    }
    const size_t idx = std::min(index, scene_presets_.size() - 1);
    const auto& preset = scene_presets_[idx];
    scene_preset_selection_ = idx;
    [[maybe_unused]] const demo_scene_compat::DemoSceneEntityGuids guids =
        demo_scene_compat::apply_demo_preset_to_scene(ctx.scenes(), preset, ctx.resource_dir());
  }

  void imgui_meshlet_renderer(teng::engine::EngineContext& ctx) {
    ImGui::Begin("Meshlet renderer");
    if (!scene_presets_.empty()) {
      ImGui::SeparatorText("Scene presets");
      const float list_h = ImGui::GetTextLineHeightWithSpacing() * 7.0f;
      if (ImGui::BeginListBox("##scene_presets", ImVec2(-FLT_MIN, list_h))) {
        for (size_t i = 0; i < scene_presets_.size(); ++i) {
          ImGui::PushID(static_cast<int>(i));
          const bool selected = (scene_preset_selection_ == i);
          if (ImGui::Selectable(scene_presets_[i].name.c_str(), selected,
                                ImGuiSelectableFlags_AllowDoubleClick)) {
            scene_preset_selection_ = i;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
              apply_demo_scene_preset(ctx, i);
            }
          }
          ImGui::PopID();
        }
        ImGui::EndListBox();
      }
      if (ImGui::Button("Load preset", ImVec2(-FLT_MIN, 0))) {
        apply_demo_scene_preset(ctx, scene_preset_selection_);
      }
      ImGui::Separator();
    }
    ctx.renderer().on_imgui();
    ImGui::End();
  }

  std::vector<teng::demo_scenes::DemoScenePresetData> scene_presets_;
  size_t scene_preset_selection_{};
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
