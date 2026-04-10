#include "TestRenderer.hpp"

#include <GLFW/glfw3.h>

#include <tracy/Tracy.hpp>
#include <vector>

#include "ResourceManager.hpp"
#include "UI.hpp"
#include "Window.hpp"
#include "core/Logger.hpp"  // IWYU pragma: keep
#include "gfx/ModelGPUManager.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/RendererCVars.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "hlsl/material.h"
#include "hlsl/shader_constants.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "implot.h"

using namespace teng;
using namespace teng::gfx;
using namespace teng::gfx::rhi;

namespace teng::gfx {

TestRenderer::TestRenderer(const CreateInfo& cinfo)
    : active_scene_(cinfo.initial_scene),
      device_(cinfo.device),
      swapchain_(cinfo.swapchain),
      frame_gpu_upload_allocator_(device_, false),
      resource_dir_(cinfo.resource_dir),
      buffer_copy_mgr_(device_, frame_gpu_upload_allocator_),
      window_(cinfo.window),
      static_instance_mgr_(*device_, buffer_copy_mgr_, device_->get_info().frames_in_flight, true),
      static_draw_batch_(GeometryBatchType::Static, *device_, buffer_copy_mgr_,
                         GeometryBatch::CreateInfo{
                             .initial_vertex_capacity = 1'000'000,
                             .initial_index_capacity = 1'000'000,
                             .initial_meshlet_capacity = 1'000'000,
                             .initial_mesh_capacity = 100'000,
                             .initial_meshlet_triangle_capacity = 1'000'000,
                             .initial_meshlet_vertex_capacity = 1'000'000,
                         }),
      materials_buf_(*device_, buffer_copy_mgr_,
                     {.usage = rhi::BufferUsage::Storage,
                      .size = k_max_materials * sizeof(M4Material),
                      // .flags = rhi::BufferDescFlags::DisableCPUAccessOnUMA,
                      .name = "all materials buf"},
                     sizeof(M4Material)) {
  shader_mgr_ = std::make_unique<gfx::ShaderManager>();
  shader_mgr_->init(
      device_, gfx::ShaderManager::Options{.targets = device_->get_supported_shader_targets()});
  renderer_cv::pipeline_mesh_shaders.set(1);
  imgui_renderer_ = std::make_unique<ImGuiRenderer>(*shader_mgr_, device_);
  rg_.init(device_);
  model_gpu_mgr_ = std::make_unique<ModelGPUMgr>(*device_, static_instance_mgr_, static_draw_batch_,
                                                 buffer_copy_mgr_, materials_buf_);
  ctx_ = {
      .device = device_,
      .swapchain = swapchain_,
      .window = window_,
      .shader_mgr = shader_mgr_.get(),
      .rg = &rg_,
      .buffer_copy = &buffer_copy_mgr_,
      .frame_staging = &frame_gpu_upload_allocator_,
      .imgui_renderer = imgui_renderer_.get(),
      .model_gpu_mgr = model_gpu_mgr_.get(),
      .resource_dir = resource_dir_,
  };
  update_ctx();
  init_imgui();
}

void TestRenderer::update_ctx() {
  ctx_.time_sec = static_cast<float>(glfwGetTime());
  ctx_.curr_frame_idx = (ctx_.curr_frame_idx + 1) % k_max_frames_in_flight;
}

void TestRenderer::set_scene(TestDebugScene id) {
  if (scene_) {
    scene_->shutdown();
    scene_.reset();
  }
  active_scene_ = id;
  scene_ = create_test_scene(id, ctx_);
  LINFO("vktest scene: {}", to_string(id));
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

void TestRenderer::render(bool imgui_ui_active) {
  ZoneScoped;
  update_ctx();
  if (!have_prev_time_) {
    ctx_.delta_time_sec = 0.f;
    have_prev_time_ = true;
  } else {
    ctx_.delta_time_sec = ctx_.time_sec - prev_time_sec_;
  }
  prev_time_sec_ = ctx_.time_sec;
  ctx_.imgui_ui_active = imgui_ui_active;

  if (scene_) {
    scene_->on_frame(ctx_);
  }

  model_gpu_mgr_->set_curr_frame_idx(ctx_.curr_frame_idx);
  shader_mgr_->replace_dirty_pipelines();
  add_render_graph_passes();
  static int i = 0;
  bool verbose = i++ == 0;
  {
    ZoneScopedN("acquire_next_swapchain_image");
    device_->acquire_next_swapchain_image(swapchain_);
  }

  {
    ZoneScopedN("bake");
    rg_.bake(window_->get_window_size(), verbose);
  }

  // ModelGPUMgr uploads use BufferCopyMgr::copy_to_buffer. On discrete GPUs, geometry buffers are
  // not host-mapped, so that enqueues vkCmdCopyBuffer work. MemeRenderer drains these before the
  // graph; vktest must do the same or mesh/task draws read empty buffers (blank meshlet scene).
  std::vector<CmdEncoder*> wait_for_encoders;
  if (!buffer_copy_mgr_.get_copies().empty()) {
    ZoneScopedN("buffer_upload_copies");
    auto* copy_enc = device_->begin_cmd_encoder(QueueType::Graphics);
    for (const auto& copy : buffer_copy_mgr_.get_copies()) {
      if (!copy.src_buf.is_valid() || !device_->get_buf(copy.src_buf)) {
        continue;
      }
      if (!copy.dst_buf.is_valid() || !device_->get_buf(copy.dst_buf)) {
        continue;
      }
      copy_enc->barrier(copy.src_buf, PipelineStage::AllCommands, AccessFlags::AnyWrite,
                        PipelineStage::AllTransfer, AccessFlags::TransferRead);
      copy_enc->barrier(copy.dst_buf, PipelineStage::AllCommands,
                        AccessFlags::AnyRead | AccessFlags::AnyWrite, PipelineStage::AllTransfer,
                        AccessFlags::TransferWrite);
      copy_enc->copy_buffer_to_buffer(copy.src_buf, copy.src_offset, copy.dst_buf, copy.dst_offset,
                                      copy.size);
      copy_enc->barrier(copy.dst_buf, PipelineStage::AllTransfer, AccessFlags::TransferWrite,
                        copy.dst_stage, copy.dst_access);
    }
    buffer_copy_mgr_.clear_copies();
    copy_enc->end_encoding();
    wait_for_encoders.push_back(copy_enc);
  }

  {
    ZoneScopedN("execute");
    auto* enc = device_->begin_cmd_encoder();
    for (auto* wait_enc : wait_for_encoders) {
      device_->cmd_encoder_wait_for(wait_enc, enc);
    }
    imgui_renderer_->flush_pending_texture_uploads(enc, frame_gpu_upload_allocator_);
    rg_.execute(enc);
    enc->end_encoding();
  }

  device_->submit_frame();

  ctx_.curr_frame_idx = (ctx_.curr_frame_idx + 1) % k_max_frames_in_flight;
}

void TestRenderer::shutdown() {
  if (scene_) {
    scene_->shutdown();
    scene_.reset();
  }
  rg_.shutdown();
  imgui_renderer_->shutdown();
  shader_mgr_->shutdown();
}

TestRenderer::~TestRenderer() = default;

void TestRenderer::recreate_resources_on_swapchain_resize() {
  if (scene_) {
    scene_->on_swapchain_resize();
  }
}

void TestRenderer::add_render_graph_passes() {
  ZoneScoped;
  scene_->add_render_graph_passes();
}

void TestRenderer::init_imgui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  for (const auto& entry : std::filesystem::directory_iterator(resource_dir_ / "fonts")) {
    if (entry.path().extension() == ".ttf") {
      auto* font = io.Fonts->AddFontFromFileTTF(entry.path().string().c_str(), 16, nullptr,
                                                io.Fonts->GetGlyphRangesDefault());
      add_font(entry.path().stem().string(), font);
    }
  }

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.BackendRendererName = "imgui_impl_memes";
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
  ImGui_ImplGlfw_InitForOther(window_->get_handle(), true);
}

}  // namespace teng::gfx
