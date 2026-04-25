#include "engine/render/RenderService.hpp"

#include <algorithm>
#include <memory>

#include "Window.hpp"
#include "core/EAssert.hpp"
#include "engine/Engine.hpp"
#include "engine/render/IRenderer.hpp"
#include "engine/render/RenderSceneExtractor.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneManager.hpp"
#include "gfx/ShaderManager.hpp"
#include "gfx/rhi/CmdEncoder.hpp"
#include "gfx/rhi/Device.hpp"
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
  render_graph_.init(device_);

  frame_ = {};
  frame_.device = device_;
  frame_.swapchain = swapchain_;
  frame_.render_graph = &render_graph_;
  frame_.shader_mgr = shader_mgr_.get();
  frame_.resource_dir = &resource_dir_;
  frame_.time = time_;
  frame_.imgui_ui_active = cinfo.imgui_ui_active;
  update_frame_context();
  initialized_ = true;
}

void RenderService::shutdown() {
  if (!initialized_) {
    return;
  }
  renderer_.reset();
  render_graph_.shutdown();
  shader_mgr_->shutdown();
  shader_mgr_.reset();
  frame_ = {};
  device_ = nullptr;
  swapchain_ = nullptr;
  window_ = nullptr;
  scenes_ = nullptr;
  time_ = nullptr;
  initialized_ = false;
}

void RenderService::set_renderer(std::unique_ptr<IRenderer> renderer) {
  renderer_ = std::move(renderer);
  if (renderer_ && initialized_) {
    renderer_->on_resize(frame_);
  }
}

void RenderService::render_active_scene() {
  ASSERT(initialized_);

  update_frame_context();
  Scene* active_scene = scenes_->active_scene();
  last_extracted_scene_ =
      active_scene ? extract_render_scene(*active_scene, {.frame = RenderSceneFrame{
                                                     .frame_index = frame_.frame_index,
                                                     .delta_seconds = time_->delta_seconds,
                                                     .output_extent = frame_.output_extent,
                                                 }})
                   : RenderScene{.frame = RenderSceneFrame{
                                      .frame_index = frame_.frame_index,
                                      .delta_seconds = time_->delta_seconds,
                                      .output_extent = frame_.output_extent,
                                  }};
  render_scene(last_extracted_scene_);
}

void RenderService::render_scene(const RenderScene& scene) {
  ASSERT(initialized_);
  ASSERT(renderer_ != nullptr);

  update_frame_context();
  shader_mgr_->replace_dirty_pipelines();
  renderer_->render(frame_, scene);
  render_graph_.bake(frame_.output_extent, false);
  execute_graph();
  frame_.curr_frame_in_flight_idx =
      (frame_.curr_frame_in_flight_idx + 1) % std::max<std::uint32_t>(1, device_->frames_in_flight());
}

void RenderService::request_render_graph_debug_dump() { render_graph_.request_debug_dump_once(); }

void RenderService::recreate_resources_on_swapchain_resize() {
  if (renderer_) {
    update_frame_context();
    renderer_->on_resize(frame_);
  }
}

void RenderService::update_frame_context() {
  const glm::ivec2 window_size = window_->get_window_size();
  frame_.output_extent = {
      static_cast<std::uint32_t>(std::max(window_size.x, 0)),
      static_cast<std::uint32_t>(std::max(window_size.y, 0)),
  };
  frame_.frame_index = time_ ? time_->frame_index : 0;
  frame_.time = time_;
  frame_.device = device_;
  frame_.swapchain = swapchain_;
  frame_.render_graph = &render_graph_;
  frame_.shader_mgr = shader_mgr_.get();
  frame_.resource_dir = &resource_dir_;
}

void RenderService::execute_graph() {
  device_->acquire_next_swapchain_image(swapchain_);
  auto* enc = device_->begin_cmd_encoder();
  render_graph_.execute(enc);
  enc->end_encoding();
  device_->submit_frame();
}

}  // namespace teng::engine
