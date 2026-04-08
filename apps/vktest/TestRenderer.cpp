#include "TestRenderer.hpp"

#include <GLFW/glfw3.h>

#include <tracy/Tracy.hpp>

#include "UI.hpp"
#include "Window.hpp"
#include "core/Logger.hpp"  // IWYU pragma: keep
#include "gfx/ShaderManager.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Swapchain.hpp"
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
      window_(cinfo.window) {
  shader_mgr_ = std::make_unique<gfx::ShaderManager>();
  shader_mgr_->init(
      device_, gfx::ShaderManager::Options{.targets = device_->get_supported_shader_targets()});
  imgui_renderer_ = std::make_unique<ImGuiRenderer>(*shader_mgr_, device_);
  rg_.init(device_);
  ctx_ = {
      .device = device_,
      .swapchain = swapchain_,
      .window = window_,
      .shader_mgr = shader_mgr_.get(),
      .rg = &rg_,
      .buffer_copy = &buffer_copy_mgr_,
      .frame_staging = &frame_gpu_upload_allocator_,
      .imgui_renderer = imgui_renderer_.get(),
  };
  update_ctx();
  scene_ = create_test_scene(active_scene_, ctx_);
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

void TestRenderer::render() {
  ZoneScoped;
  update_ctx();
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

  {
    ZoneScopedN("execute");
    auto* enc = device_->begin_cmd_encoder();
    imgui_renderer_->flush_pending_texture_uploads(enc, frame_gpu_upload_allocator_);
    rg_.execute(enc);
    enc->end_encoding();
  }

  device_->submit_frame();

  ctx_.curr_frame_idx = (ctx_.curr_frame_idx + 1) % k_max_frames_in_flight;
}

TestRenderer::~TestRenderer() {
  imgui_renderer_->shutdown();
  if (scene_) {
    scene_->shutdown();
    scene_.reset();
  }
  shader_mgr_->shutdown();
}

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
