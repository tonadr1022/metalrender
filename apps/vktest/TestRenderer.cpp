#include "TestRenderer.hpp"

#include <GLFW/glfw3.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <tracy/Tracy.hpp>

#include "../common/ScenePresets.hpp"
#include "TestDebugScenes.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"  // IWYU pragma: keep
#include "engine/Engine.hpp"
#include "engine/render/RenderFrameContext.hpp"
#include "engine/render/RenderScene.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "imgui.h"

using namespace teng;
using namespace teng::gfx;
using namespace teng::gfx::rhi;

namespace teng::gfx {

TestRenderer::TestRenderer(const CreateInfo& cinfo) : active_scene_(cinfo.initial_scene) {}

void TestRenderer::populate_compatibility_context(engine::RenderFrameContext& frame) {
  ctx_.device = frame.device;
  ctx_.swapchain = frame.swapchain;
  ctx_.window = frame.window;
  ctx_.shader_mgr = frame.shader_mgr;
  ctx_.rg = frame.render_graph;
  ctx_.buffer_copy = frame.buffer_copy;
  ctx_.frame_staging = frame.frame_staging;
  ctx_.imgui_renderer = nullptr;
  ctx_.render_imgui_overlay = [imgui_renderer = frame.imgui_renderer, swapchain = frame.swapchain,
                               frame_in_flight = frame.curr_frame_in_flight_idx,
                               imgui_ui_active = frame.imgui_ui_active](rhi::CmdEncoder* enc) {
    if (!imgui_ui_active || imgui_renderer == nullptr || swapchain == nullptr) {
      return;
    }
    imgui_renderer->render(enc, {swapchain->desc_.width, swapchain->desc_.height}, frame_in_flight);
  };
  ctx_.model_gpu_mgr = frame.model_gpu_mgr;
  ctx_.curr_frame_in_flight_idx = frame.curr_frame_in_flight_idx;
  ctx_.resource_dir = frame.resource_dir ? *frame.resource_dir : std::filesystem::path{};
  ctx_.time_sec = frame.time ? static_cast<float>(frame.time->total_seconds)
                             : static_cast<float>(glfwGetTime());
  ctx_.delta_time_sec = frame.time ? frame.time->delta_seconds : 0.f;
  ctx_.imgui_ui_active = frame.imgui_ui_active;
}

void TestRenderer::set_scene(TestDebugScene id) {
  if (scene_) {
    scene_->shutdown();
    scene_.reset();
  }
  active_scene_ = id;
  scene_ = create_test_scene(id, ctx_);
  if (id == TestDebugScene::MeshletRenderer) {
    teng::demo_scenes::seed_demo_scene_rng(10000000);
    scene_->apply_demo_scene_preset(0);
  }
  LINFO("vktest scene: {}", to_string(id));
}

void TestRenderer::apply_demo_scene_preset(size_t index) {
  ASSERT(scene_);
  scene_->apply_demo_scene_preset(index);
}

void TestRenderer::cycle_debug_scene() {
  auto next = static_cast<uint8_t>(static_cast<uint8_t>(active_scene_) + 1u) %
              static_cast<uint8_t>(TestDebugScene::Count);
  set_scene(static_cast<TestDebugScene>(next));
}

void TestRenderer::on_cursor_pos(double x, double y) {
  if (scene_) {
    scene_->on_cursor_pos(x, y);
  }
}

void TestRenderer::on_key_event(int key, int action, int mods) {
  if (scene_) {
    scene_->on_key_event(key, action, mods);
  }
}

void TestRenderer::render(engine::RenderFrameContext& frame, const engine::RenderScene&) {
  ZoneScoped;
  populate_compatibility_context(frame);
  if (!have_prev_time_) {
    ctx_.delta_time_sec = 0.f;
    have_prev_time_ = true;
  } else {
    ctx_.delta_time_sec = ctx_.time_sec - prev_time_sec_;
  }
  prev_time_sec_ = ctx_.time_sec;

  if (scene_) {
    scene_->on_frame(ctx_);
  }

  ctx_.model_gpu_mgr->set_curr_frame_idx(ctx_.curr_frame_in_flight_idx);

  add_render_graph_passes();
}

void TestRenderer::shutdown() {
  if (scene_) {
    scene_->shutdown();
    scene_.reset();
  }
}

TestRenderer::~TestRenderer() = default;

void TestRenderer::on_resize(engine::RenderFrameContext& frame) {
  populate_compatibility_context(frame);
  if (scene_) {
    scene_->on_swapchain_resize();
  }
}

void TestRenderer::add_render_graph_passes() {
  ZoneScoped;
  ASSERT(ctx_.model_gpu_mgr != nullptr);
  auto& static_instance_mgr = ctx_.model_gpu_mgr->instance_mgr();
  if (static_instance_mgr.has_pending_frees(ctx_.curr_frame_in_flight_idx)) {
    auto instance_data_id = ctx_.rg->import_external_buffer(
        static_instance_mgr.get_instance_data_buf(),
        RGState{.stage = PipelineStage::TopOfPipe, .layout = ResourceLayout::General},
        "instance_data_buf");
    auto& p = ctx_.rg->add_transfer_pass("free_instance_data");
    p.write_buf(instance_data_id, PipelineStage::AllTransfer);
    p.set_ex([this, &static_instance_mgr](CmdEncoder* enc) {
      static_instance_mgr.flush_pending_frees(ctx_.curr_frame_in_flight_idx, enc);
    });
  }
  scene_->add_render_graph_passes();
}

void TestRenderer::imgui_scene_overlay() {
  imgui_device_info();
  if (scene_) {
    scene_->on_imgui();
  }
}

namespace {

const char* gpu_adapter_kind_str(rhi::GpuAdapterKind k) {
  switch (k) {
    case rhi::GpuAdapterKind::Integrated:
      return "Integrated";
    case rhi::GpuAdapterKind::Discrete:
      return "Discrete";
    case rhi::GpuAdapterKind::Virtual:
      return "Virtual";
    case rhi::GpuAdapterKind::Cpu:
      return "CPU";
    case rhi::GpuAdapterKind::Other:
      return "Other";
    case rhi::GpuAdapterKind::Unknown:
    default:
      return "Unknown";
  }
}

}  // namespace

void TestRenderer::imgui_device_info() const {
  if (ctx_.device == nullptr) {
    return;
  }
  const rhi::GpuAdapterInfo info = ctx_.device->query_gpu_adapter_info();
  ImGui::Separator();
  ImGui::TextUnformatted("GPU / adapter");
  if (!info.name.empty()) {
    ImGui::TextWrapped("Name: %s", info.name.c_str());
  } else {
    ImGui::TextUnformatted("Name: (unavailable)");
  }
  ImGui::Text("Kind: %s", gpu_adapter_kind_str(info.kind));
  if (!info.api_version.empty()) {
    ImGui::Text("API: %s", info.api_version.c_str());
  }
  if (!info.driver_version.empty()) {
    ImGui::Text("Driver: %s", info.driver_version.c_str());
  }
  if (info.vendor_id != 0 || info.device_id != 0) {
    ImGui::Text("Vendor ID: 0x%08X  Device ID: 0x%08X", info.vendor_id, info.device_id);
  }
}

}  // namespace teng::gfx
