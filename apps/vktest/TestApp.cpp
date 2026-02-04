#include "TestApp.hpp"

#include "TestRenderer.hpp"
#include "Util.hpp"
#include "core/Logger.hpp"
#include "gfx/rhi/Device.hpp"
#include "gfx/rhi/Swapchain.hpp"

using namespace teng;
using namespace teng;

TestApp::TestApp() {
  resource_dir_ = get_resource_dir();
  std::filesystem::current_path(resource_dir_.parent_path());
  LINFO("Current path: {}", std::filesystem::current_path().string());

  window_ = create_platform_window();

  Window::InitInfo win_init_info{
      .win_dims_x = 1000,
      .win_dims_y = 1000,
      .floating_window = false,
  };
  window_->init(win_init_info);
  window_->set_window_position({500, 0});
  // device_ = rhi::create_device(rhi::GfxAPI::Vulkan);
  device_ = rhi::create_device(rhi::GfxAPI::Metal);

  device_->init({
      .shader_lib_dir = resource_dir_ / "shader_out",
      .app_name = "lol",
      .frames_in_flight = 3,
  });

  auto win_dims = window_->get_window_size();
  swapchain_ = device_->create_swapchain_h(rhi::SwapchainDesc{
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
}

TestApp::~TestApp() = default;

void TestApp::run() {
  while (!window_->should_close()) {
    window_->poll_events();
    renderer_->render();
  }

  renderer_.reset();
  window_->shutdown();
  swapchain_ = {};
  device_->shutdown();
}
