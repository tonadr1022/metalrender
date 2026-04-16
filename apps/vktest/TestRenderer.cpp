#include "TestRenderer.hpp"

#include <GLFW/glfw3.h>

#include <tracy/Tracy.hpp>
#include <vector>

#include "../common/ScenePresets.hpp"
#include "ResourceManager.hpp"
#include "UI.hpp"
#include "Window.hpp"
#include "core/Logger.hpp"  // IWYU pragma: keep
#include "gfx/ModelGPUManager.hpp"
#include "gfx/ShaderManager.hpp"
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

  {
    LINFO("making samplers");
    samplers_.emplace_back(device_->create_sampler_h({
        .min_filter = rhi::FilterMode::Nearest,
        .mag_filter = rhi::FilterMode::Nearest,
        .mipmap_mode = rhi::FilterMode::Nearest,
        .address_mode = rhi::AddressMode::Repeat,
    }));
    samplers_.emplace_back(device_->create_sampler_h({
        .min_filter = rhi::FilterMode::Linear,
        .mag_filter = rhi::FilterMode::Linear,
        .mipmap_mode = rhi::FilterMode::Linear,
        .address_mode = rhi::AddressMode::Repeat,
    }));
    samplers_.emplace_back(device_->create_sampler_h({
        .min_filter = rhi::FilterMode::Nearest,
        .mag_filter = rhi::FilterMode::Nearest,
        .mipmap_mode = rhi::FilterMode::Nearest,
        .address_mode = rhi::AddressMode::ClampToEdge,
    }));
  }
}

void TestRenderer::update_ctx() { ctx_.time_sec = static_cast<float>(glfwGetTime()); }

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

  model_gpu_mgr_->set_curr_frame_idx(ctx_.curr_frame_in_flight_idx);
  shader_mgr_->replace_dirty_pipelines();

  add_render_graph_passes();
  static int i = 0;
  bool verbose = i++ == 0;
  rg_.bake(window_->get_window_size(), verbose);

  device_->acquire_next_swapchain_image(swapchain_);

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
  }

  {
    ZoneScopedN("execute");
    auto* enc = device_->begin_cmd_encoder();
    imgui_renderer_->flush_pending_texture_uploads(enc, frame_gpu_upload_allocator_);
    {
      const auto& pending = model_gpu_mgr_->get_pending_texture_uploads();
      if (!pending.empty()) {
        for (const auto& upload : pending) {
          upload_texture_data(upload, device_->get_tex(upload.tex), frame_gpu_upload_allocator_,
                              enc);
        }
        model_gpu_mgr_->clear_pending_texture_uploads();
      }
    }
    rg_.execute(enc);
    enc->end_encoding();
  }

  device_->submit_frame();

  ctx_.curr_frame_in_flight_idx = (ctx_.curr_frame_in_flight_idx + 1) % k_max_frames_in_flight;
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

void TestRenderer::imgui_device_info() {
  const rhi::GpuAdapterInfo info = device_->query_gpu_adapter_info();
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

void TestRenderer::request_render_graph_debug_dump() { rg_.request_debug_dump_once(); }

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
