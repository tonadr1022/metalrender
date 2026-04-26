#include "TestRenderer.hpp"

#include <GLFW/glfw3.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <tracy/Tracy.hpp>

#include "../common/ScenePresets.hpp"
#include "DemoSceneEcsBridge.hpp"
#include "TestDebugScenes.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"  // IWYU pragma: keep
#include "engine/Engine.hpp"
#include "engine/render/RenderFrameContext.hpp"
#include "engine/render/RenderScene.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/MeshletRenderer.hpp"
#include "scenes/MeshletRendererTestScene.hpp"

using namespace teng;
using namespace teng::gfx;
using namespace teng::gfx::rhi;

namespace teng::gfx {

TestRenderer::TestRenderer(const CreateInfo& cinfo) : active_scene_(cinfo.initial_scene) {}

void TestRenderer::populate_compatibility_context(engine::RenderFrameContext& frame) {
  ZoneScoped;
  ctx_.device = frame.device;
  ctx_.swapchain = frame.swapchain;
  ctx_.curr_swapchain_rg_id = frame.curr_swapchain_rg_id;
  ctx_.window = frame.window;
  ctx_.shader_mgr = frame.shader_mgr;
  ctx_.rg = frame.render_graph;
  ctx_.buffer_copy = frame.buffer_copy;
  ctx_.frame_staging = frame.frame_staging;
  ctx_.model_gpu_mgr = frame.model_gpu_mgr;
  ctx_.scene_manager = frame.scenes;
  ctx_.curr_frame_in_flight_idx = frame.curr_frame_in_flight_idx;
  ctx_.resource_dir = frame.resource_dir ? *frame.resource_dir : std::filesystem::path{};
  ctx_.time_sec = frame.time ? static_cast<float>(frame.time->total_seconds)
                             : static_cast<float>(glfwGetTime());
  ctx_.delta_time_sec = frame.time ? frame.time->delta_seconds : 0.f;
  ctx_.imgui_ui_active = frame.imgui_ui_active;
}

void TestRenderer::set_scene(TestDebugScene id) {
  if (scene_) {
    if (auto* mrs = dynamic_cast<MeshletRendererScene*>(scene_.get())) {
      mrs->set_meshlet_renderer(nullptr);
    }
    scene_->shutdown();
    scene_.reset();
  }
  meshlet_path_renderer_.reset();
  active_scene_ = id;
  scene_ = create_test_scene(id, ctx_);
  if (id == TestDebugScene::MeshletRenderer) {
    meshlet_path_renderer_ = std::make_unique<MeshletRenderer>();
    if (auto* mrs = dynamic_cast<MeshletRendererScene*>(scene_.get())) {
      mrs->set_meshlet_renderer(meshlet_path_renderer_.get());
    }
    teng::demo_scenes::seed_demo_scene_rng(10000000);
    scene_->apply_demo_scene_preset(0);
  }
  LINFO("vktest scene: {}", to_string(id));
}

void TestRenderer::update(engine::RenderFrameContext& frame) {
  ZoneScoped;
  populate_compatibility_context(frame);
  if (!have_prev_time_) {
    ctx_.delta_time_sec = 0.f;
    have_prev_time_ = true;
  } else {
    ctx_.delta_time_sec = ctx_.time_sec - prev_time_sec_;
  }
  prev_time_sec_ = ctx_.time_sec;

  if (!scene_) {
    return;
  }
  scene_->on_frame(ctx_);
  if (ctx_.scene_manager) {
    auto* active_scene = ctx_.scene_manager->active_scene();
    if (!active_scene) {
      active_scene = &ctx_.scene_manager->create_scene("vktest compatibility render scene");
    }
  }
}

void TestRenderer::apply_demo_scene_preset(size_t index) {
  ASSERT(scene_);
  scene_->apply_demo_scene_preset(index);
}

void TestRenderer::render(engine::RenderFrameContext& frame, const engine::RenderScene& scene) {
  ZoneScoped;
  populate_compatibility_context(frame);
  if (meshlet_path_renderer_) {
    meshlet_path_renderer_->render(frame, scene);
    return;
  }
  ctx_.model_gpu_mgr->set_curr_frame_idx(ctx_.curr_frame_in_flight_idx);
}

void TestRenderer::shutdown() {
  scene_->shutdown();
  scene_.reset();
  meshlet_path_renderer_.reset();
}

TestRenderer::~TestRenderer() = default;

void TestRenderer::on_resize(engine::RenderFrameContext& frame) {
  populate_compatibility_context(frame);
  if (meshlet_path_renderer_) {
    meshlet_path_renderer_->on_resize(frame);
  }
}

void TestRenderer::imgui_scene_overlay() { scene_->on_imgui(); }

}  // namespace teng::gfx
