#include "TestApp.hpp"

#include <GLFW/glfw3.h>

#include <tracy/Tracy.hpp>

#include "ResourceManager.hpp"
#include "TestRenderer.hpp"
#include "Util.hpp"
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

  window_ = create_platform_window();

  ::teng::Window::InitInfo win_init_info{
      .win_dims_x = 1000,
      .win_dims_y = 1000,
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

  window_->set_key_callback([this](int key, int action, int mods) {
    if (action == GLFW_PRESS && key == GLFW_KEY_TAB) {
      renderer_->cycle_debug_scene();
    }
    if (key == GLFW_KEY_G && mods & GLFW_MOD_ALT) {
      imgui_enabled_ = !imgui_enabled_;
    }
  });

  //  ResourceManager::init(ResourceManager::CreateInfo{.renderer = renderer_});
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

    renderer_->render();

    if (imgui_enabled_) {
      ImGui::EndFrame();
    }
  }

  ResourceManager::shutdown();
  renderer_.reset();
  swapchain_ = {};
  window_->shutdown();
  device_->shutdown();
}

void TestApp::on_imgui() const {
  ZoneScoped;
  ImGui::Begin("TestApp");
  ImGui::Text("ImGui enabled: %d", imgui_enabled_);
  ImGui::End();
}