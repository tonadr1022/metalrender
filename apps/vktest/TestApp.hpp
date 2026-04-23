#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include "TestRenderer.hpp"
#include "Window.hpp"
#include "gfx/rhi/GFXTypes.hpp"

struct TestAppOptions {
  // When set, exit the main loop after this many full frames (poll + render).
  std::optional<std::uint32_t> quit_after_frames;
};

class TestApp {
 public:
  explicit TestApp(TestAppOptions options = {});
  ~TestApp();
  void run();

 private:
  void on_imgui();
  std::filesystem::path resource_dir_;
  std::filesystem::path local_resource_dir_;
  std::unique_ptr<teng::Window> window_;
  std::unique_ptr<teng::gfx::rhi::Device> device_;
  teng::gfx::rhi::SwapchainHandleHolder swapchain_;
  std::unique_ptr<teng::gfx::TestRenderer> renderer_;
  bool imgui_enabled_{true};
  std::optional<std::uint32_t> quit_after_frames_;
  std::uint32_t completed_frames_{0};
};
