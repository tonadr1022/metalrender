#pragma once
#include <filesystem>

#include "WindowApple.hpp"
#include "gfx/RendererMetal.hpp"
#include "gfx/metal/MetalDevice.hpp"

struct App {
  App();

  App(const App &) = delete;
  App(App &&) = delete;
  App &operator=(const App &) = delete;
  App &operator=(App &&) = delete;
  ~App() = default;
  void run();

 private:
  std::filesystem::path resource_dir_;
  std::filesystem::path shader_dir_;
  std::unique_ptr<MetalDevice> device_;
  std::unique_ptr<WindowApple> window_;
  // TODO: ptr for impl/rhi
  RendererMetal renderer_;
};
