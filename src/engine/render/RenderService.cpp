#include "engine/render/RenderService.hpp"

#include <filesystem>
#include <glm/ext/vector_int2.hpp>
#include <memory>
#include <tracy/Tracy.hpp>
#include <utility>

#include "Window.hpp"
#include "core/EAssert.hpp"
#include "core/Logger.hpp"  // IWYU pragma: keep
#include "engine/Engine.hpp"
#include "engine/render/IRenderer.hpp"
#include "engine/render/RenderScene.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneManager.hpp"
#include "gfx/BackedGPUAllocator.hpp"
#include "gfx/DrawBatch.hpp"
#include "gfx/GPUFrameAllocator2.hpp"
#include "gfx/ImGuiRenderer.hpp"
#include "gfx/ModelGPUManager.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/renderer/BufferResize.hpp"
#include "gfx/renderer/InstanceMgr.hpp"
#include "gfx/renderer/ModelGPUUploader.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/GFXTypes.hpp"
#include "gfx/rhi/Swapchain.hpp"

namespace teng::engine {

RenderService::RenderService(const CreateInfo& cinfo) { init(cinfo); }

RenderService::~RenderService() { shutdown(); }

void RenderService::init(const CreateInfo& cinfo) {
  ASSERT(!initialized_);
  ASSERT(cinfo.device != nullptr);
  ASSERT(cinfo.swapchain != nullptr);
  ASSERT(cinfo.window != nullptr);
  ASSERT(cinfo.scenes != nullptr);
  ASSERT(cinfo.time != nullptr);

  device_ = cinfo.device;
  swapchain_ = cinfo.swapchain;
  window_ = cinfo.window;
  scenes_ = cinfo.scenes;
  time_ = cinfo.time;
  resource_dir_ = cinfo.resource_dir;

  shader_mgr_ = std::make_unique<gfx::ShaderManager>();
  shader_mgr_->init(
      device_, gfx::ShaderManager::Options{.targets = device_->get_supported_shader_targets()});
  frame_gpu_upload_allocator_ = std::make_unique<gfx::GPUFrameAllocator3>(device_, false);
  buffer_copy_mgr_ = std::make_unique<gfx::BufferCopyMgr>(device_, *frame_gpu_upload_allocator_);
  imgui_renderer_ = std::make_unique<gfx::ImGuiRenderer>(*shader_mgr_, device_);
  render_graph_.init(device_);
  model_gpu_mgr_ = std::make_unique<gfx::ModelGPUMgr>(*device_, *buffer_copy_mgr_);

  frame_ = {};
  frame_.device = device_;
  frame_.swapchain = swapchain_;
  frame_.window = window_;
  frame_.render_graph = &render_graph_;
  frame_.shader_mgr = shader_mgr_.get();
  frame_.buffer_copy = buffer_copy_mgr_.get();
  frame_.frame_staging = frame_gpu_upload_allocator_.get();
  frame_.model_gpu_mgr = model_gpu_mgr_.get();
  frame_.scenes = scenes_;
  frame_.resource_dir = &resource_dir_;
  frame_.time = time_;
  frame_.imgui_ui_active = cinfo.imgui_ui_active;
  update_frame_context();

  LINFO("making samplers");
  samplers_.emplace_back(device_->create_sampler_h({
      .min_filter = gfx::rhi::FilterMode::Nearest,
      .mag_filter = gfx::rhi::FilterMode::Nearest,
      .mipmap_mode = gfx::rhi::FilterMode::Nearest,
      .address_mode = gfx::rhi::AddressMode::Repeat,
  }));
  samplers_.emplace_back(device_->create_sampler_h({
      .min_filter = gfx::rhi::FilterMode::Linear,
      .mag_filter = gfx::rhi::FilterMode::Linear,
      .mipmap_mode = gfx::rhi::FilterMode::Linear,
      .address_mode = gfx::rhi::AddressMode::Repeat,
  }));
  samplers_.emplace_back(device_->create_sampler_h({
      .min_filter = gfx::rhi::FilterMode::Nearest,
      .mag_filter = gfx::rhi::FilterMode::Nearest,
      .mipmap_mode = gfx::rhi::FilterMode::Nearest,
      .address_mode = gfx::rhi::AddressMode::ClampToEdge,
  }));
  initialized_ = true;
}

void RenderService::shutdown() {
  if (!initialized_) {
    return;
  }
  renderer_.reset();
  samplers_.clear();
  model_gpu_mgr_.reset();
  render_graph_.shutdown();
  shutdown_imgui_renderer();
  buffer_copy_mgr_.reset();
  frame_gpu_upload_allocator_.reset();
  shader_mgr_->shutdown();
  shader_mgr_.reset();
  frame_ = {};
  device_ = nullptr;
  swapchain_ = nullptr;
  window_ = nullptr;
  scenes_ = nullptr;
  time_ = nullptr;
  frame_open_ = false;
  initialized_ = false;
}

void RenderService::shutdown_imgui_renderer() {
  if (imgui_renderer_) {
    imgui_renderer_->shutdown();
    imgui_renderer_.reset();
  }
}

void RenderService::set_renderer(std::unique_ptr<IRenderer> renderer) {
  renderer_ = std::move(renderer);
  if (renderer_ && initialized_) {
    renderer_->on_resize(frame_);
  }
}

void RenderService::begin_frame() {
  ZoneScoped;
  ASSERT(initialized_);
  ASSERT(!frame_open_);

  update_frame_context();
  shader_mgr_->replace_dirty_pipelines();
  device_->acquire_next_swapchain_image(swapchain_);
  frame_.curr_swapchain_rg_id = render_graph_.import_external_texture(
      swapchain_->get_current_texture(),
      gfx::RGState{.stage = gfx::rhi::PipelineStage::BottomOfPipe,
                   .layout = gfx::rhi::ResourceLayout::Undefined},
      "swapchain");
  frame_open_ = true;
}

void RenderService::enqueue_active_scene() {
  ZoneScoped;
  ASSERT(initialized_);
  ASSERT(frame_open_);
  ASSERT(renderer_ != nullptr);

  Scene* active_scene = scenes_->active_scene();
  last_extracted_scene_ =
      active_scene
          ? extract_render_scene(*active_scene, {.frame =
                                                     RenderSceneFrame{
                                                         .frame_index = frame_.frame_index,
                                                         .delta_seconds = time_->delta_seconds,
                                                         .output_extent = frame_.output_extent,
                                                     }})
          : RenderScene{.frame = RenderSceneFrame{
                            .frame_index = frame_.frame_index,
                            .delta_seconds = time_->delta_seconds,
                            .output_extent = frame_.output_extent,
                        }};
  renderer_->render(frame_, last_extracted_scene_);
}

void RenderService::enqueue_imgui_overlay_pass() {
  ZoneScoped;
  ASSERT(initialized_);
  ASSERT(frame_open_);
  ASSERT(imgui_renderer_);
  if (!frame_.imgui_ui_active) {
    return;
  }

  auto& p = render_graph_.add_graphics_pass("imgui_overlay");
  frame_.curr_swapchain_rg_id = p.w_swapchain_tex_new(swapchain_, frame_.curr_swapchain_rg_id);
  p.set_ex([imgui_renderer = imgui_renderer_.get(), swapchain = swapchain_,
            frame_in_flight = frame_.curr_frame_in_flight_idx](gfx::rhi::CmdEncoder* enc) {
    enc->begin_rendering({
        gfx::rhi::RenderAttInfo::color_att(swapchain->get_current_texture(),
                                           gfx::rhi::LoadOp::Load),
    });
    imgui_renderer->render(enc, {swapchain->desc_.width, swapchain->desc_.height}, frame_in_flight);
    enc->end_rendering();
  });
}

void RenderService::end_frame() {
  ZoneScoped;
  ASSERT(initialized_);
  ASSERT(frame_open_);

  static int i = 0;
  const bool verbose = i++ == -1;
  render_graph_.bake(frame_.output_extent, verbose);
  auto* enc = device_->begin_cmd_encoder();
  // Upload and resize migration copies are encoded in-order with render work so visibility
  // is explicit and backend-independent.
  flush_pending_buffer_copies(enc);

  flush_pending_texture_uploads(enc);

  render_graph_.execute(enc);

  enc->end_encoding();

  device_->submit_frame();

  frame_.curr_frame_in_flight_idx =
      (frame_.curr_frame_in_flight_idx + 1) % device_->frames_in_flight();
  if (frame_gpu_upload_allocator_) {
    frame_gpu_upload_allocator_->set_frame_idx_and_reset_bufs(frame_.curr_frame_in_flight_idx);
  }
  frame_open_ = false;
}

void RenderService::render_scene(const RenderScene& scene) {
  ZoneScoped;
  ASSERT(initialized_);
  ASSERT(renderer_ != nullptr);

  begin_frame();
  renderer_->render(frame_, scene);
  end_frame();
}

void RenderService::set_imgui_ui_active(bool active) { frame_.imgui_ui_active = active; }

void RenderService::request_render_graph_debug_dump() { render_graph_.request_debug_dump_once(); }

void RenderService::recreate_resources_on_swapchain_resize() {
  if (renderer_) {
    update_frame_context();
    renderer_->on_resize(frame_);
  }
}

void RenderService::update_frame_context() {
  const glm::ivec2 window_size = window_->get_window_size();
  frame_.output_extent = window_size;
  frame_.frame_index = time_ ? time_->frame_index : 0;
  frame_.time = time_;
  frame_.device = device_;
  frame_.swapchain = swapchain_;
  frame_.window = window_;
  frame_.render_graph = &render_graph_;
  frame_.shader_mgr = shader_mgr_.get();
  frame_.buffer_copy = buffer_copy_mgr_.get();
  frame_.frame_staging = frame_gpu_upload_allocator_.get();
  frame_.model_gpu_mgr = model_gpu_mgr_.get();
  frame_.scenes = scenes_;
  frame_.resource_dir = &resource_dir_;
}

void RenderService::flush_pending_buffer_copies(gfx::rhi::CmdEncoder* enc) {
  ZoneScoped;
  if (buffer_copy_mgr_->get_copies().empty()) {
    return;
  }

  for (const auto& copy : buffer_copy_mgr_->get_copies()) {
    if (!copy.src_buf.is_valid() || !device_->get_buf(copy.src_buf)) {
      continue;
    }
    if (!copy.dst_buf.is_valid() || !device_->get_buf(copy.dst_buf)) {
      continue;
    }
    enc->barrier(copy.src_buf, gfx::rhi::PipelineStage::AllCommands,
                 gfx::rhi::AccessFlags::AnyWrite, gfx::rhi::PipelineStage::AllTransfer,
                 gfx::rhi::AccessFlags::TransferRead);
    enc->barrier(copy.dst_buf, gfx::rhi::PipelineStage::AllCommands,
                 gfx::rhi::AccessFlags::AnyRead | gfx::rhi::AccessFlags::AnyWrite,
                 gfx::rhi::PipelineStage::AllTransfer, gfx::rhi::AccessFlags::TransferWrite);
    enc->copy_buffer_to_buffer(copy.src_buf, copy.src_offset, copy.dst_buf, copy.dst_offset,
                               copy.size);
    enc->barrier(copy.dst_buf, gfx::rhi::PipelineStage::AllTransfer,
                 gfx::rhi::AccessFlags::TransferWrite, copy.dst_stage, copy.dst_access);
  }
  buffer_copy_mgr_->clear_copies();
}

void RenderService::flush_pending_texture_uploads(gfx::rhi::CmdEncoder* enc) {
  if (imgui_renderer_) {
    imgui_renderer_->flush_pending_texture_uploads(enc, *frame_gpu_upload_allocator_);
  }
  if (!model_gpu_mgr_) {
    return;
  }
  const auto& pending = model_gpu_mgr_->get_pending_texture_uploads();
  for (const auto& upload : pending) {
    gfx::upload_texture_data(upload, device_->get_tex(upload.tex), *frame_gpu_upload_allocator_,
                             enc);
  }
  model_gpu_mgr_->clear_pending_texture_uploads();
}

}  // namespace teng::engine
