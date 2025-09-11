#include "App.hpp"

#include "WindowApple.hpp"
#include "gfx/metal/MetalDevice.hpp"

namespace {

std::filesystem::path get_resource_dir() {
  std::filesystem::path curr_path = std::filesystem::current_path();
  while (curr_path.has_parent_path()) {
    if (std::filesystem::exists(curr_path / "resources")) {
      return curr_path / "resources";
    }
    curr_path = curr_path.parent_path();
  }
  return "";
}

}  // namespace

App::App() {
  resource_dir_ = get_resource_dir();
  shader_dir_ = resource_dir_ / "shaders";
  device_ = create_metal_device();
  window_ = create_apple_window();
  device_->init();
  window_->init(device_.get());
  renderer_.init(RendererMetal::CreateInfo{
      .device = device_.get(), .window = window_.get(), .resource_dir = resource_dir_});
}

void App::run() {
  renderer_.load_model(resource_dir_ / "models/Cube/glTF/Cube.gltf");

  while (!window_->should_close()) {
    window_->poll_events();
    renderer_.render();
  }

  window_->shutdown();
  device_->shutdown();
}
