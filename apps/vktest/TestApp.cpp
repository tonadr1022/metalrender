#include "TestApp.hpp"

#include <GLFW/glfw3.h>

#include <filesystem>
#include <tracy/Tracy.hpp>

#include "ResourceManager.hpp"
#include "TestRenderer.hpp"
#include "Util.hpp"
#include "core/CVar.hpp"
#include "core/EAssert.hpp"
#include "gfx/RenderGraph.hpp"
#include "gfx/renderer/RendererCVars.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"

using namespace teng;
using namespace teng::gfx;

TestApp::TestApp() {
  ZoneScoped;
  resource_dir_ = get_resource_dir();
  std::filesystem::current_path(resource_dir_.parent_path());
  local_resource_dir_ = resource_dir_ / "local";
  if (!std::filesystem::exists(local_resource_dir_)) {
    std::filesystem::create_directories(local_resource_dir_);
  }
  CVarSystem::get().load_from_file((local_resource_dir_ / "cvars.txt").string());

  window_ = create_platform_window();

  ::teng::Window::InitInfo win_init_info{
      .win_dims_x = -1,
      .win_dims_y = -1,
      .floating_window = false,
  };
  window_->init(win_init_info);
  window_->set_window_position({500, 0});
  device_ = teng::gfx::rhi::create_device(gfx::rhi::GfxAPI::Vulkan);

  device_->init({
      .shader_lib_dir = resource_dir_ / "shader_out",
      .app_name = "lol",
      .frames_in_flight = 3,
  });
  ALWAYS_ASSERT(RenderGraph::run_barrier_coalesce_self_tests());
  gfx::apply_renderer_cvar_device_constraints(true);

  auto win_dims = window_->get_window_size();
  swapchain_ = device_->create_swapchain_h(gfx::rhi::SwapchainDesc{
      .window = window_.get(),
      .width = win_dims.x,
      .height = win_dims.y,
      .vsync = true,
  });
  renderer_ = std::make_unique<gfx::TestRenderer>(gfx::TestRenderer::CreateInfo{
      .device = device_.get(),
      .swapchain = device_->get_swapchain(swapchain_),
      .window = window_.get(),
      .resource_dir = resource_dir_,
  });
  ResourceManager::init(
      ResourceManager::CreateInfo{.model_gpu_mgr = renderer_->get_model_gpu_mgr()});

  renderer_->set_scene(TestDebugScene::MeshletRenderer);

  window_->set_cursor_pos_callback([this](double x, double y) { renderer_->on_cursor_pos(x, y); });
  window_->set_key_callback([this](int key, int action, int mods) {
    if (action == GLFW_PRESS && key == GLFW_KEY_TAB) {
      renderer_->cycle_debug_scene();
    }
    if (action == GLFW_PRESS && key >= GLFW_KEY_0 && key <= GLFW_KEY_9 &&
        (mods & GLFW_MOD_CONTROL) != 0) {
      renderer_->apply_demo_scene_preset(static_cast<size_t>(key - GLFW_KEY_0));
    }
    if (key == GLFW_KEY_G && mods & GLFW_MOD_ALT) {
      imgui_enabled_ = !imgui_enabled_;
    }
    renderer_->on_key_event(key, action, mods);
  });
}

TestApp::~TestApp() = default;

void TestApp::run() {
  while (!window_->should_close()) {
    ZoneScopedN("main loop");
    {
      ZoneScopedN("poll_events");
      window_->poll_events();
    }

    if (imgui_enabled_) {
      ZoneScopedN("imgui_new_frame");
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();
    }

    if (imgui_enabled_) {
      on_imgui();
    }

    if (imgui_enabled_) {
      ImGui::Render();
    }

    renderer_->render(imgui_enabled_);

    if (imgui_enabled_) {
      ImGui::EndFrame();
    }
  }

  CVarSystem::get().save_to_file((local_resource_dir_ / "cvars.txt").string());
  renderer_->shutdown();
  ResourceManager::shutdown();
  renderer_.reset();
  swapchain_ = {};
  window_->shutdown();
  device_->shutdown();
}

void TestApp::on_imgui() {
  ZoneScoped;
  ImGui::Begin("TestApp");
  ImGui::Text("ImGui enabled: %d", imgui_enabled_);
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