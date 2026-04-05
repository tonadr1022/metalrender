#pragma once

#include <filesystem>

#include "TestRenderer.hpp"
#include "Window.hpp"
#include "gfx/rhi/GFXTypes.hpp"

class TestApp {
 public:
  TestApp();
  ~TestApp();
  void run();

 private:
  void on_imgui();
  std::filesystem::path resource_dir_;
  std::unique_ptr<teng::Window> window_;
  std::unique_ptr<teng::gfx::rhi::Device> device_;
  teng::gfx::rhi::SwapchainHandleHolder swapchain_;
  std::unique_ptr<teng::gfx::TestRenderer> renderer_;
  bool imgui_enabled_{true};
};
