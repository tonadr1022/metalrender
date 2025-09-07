#pragma once

#include <memory>

#include "Window.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

namespace rhi {
class Device;
}

namespace CA {
class MetalLayer;
}

class WindowApple : public Window {
 public:
  void poll_events() override;
  void init(rhi::Device* device) override;
  void shutdown() override;
  [[nodiscard]] bool should_close() const override;
  GLFWwindow* get_handle() const { return window_; }

  // TODO: lol
  CA::MetalLayer* metal_layer_{};

 private:
  GLFWwindow* window_{};
};

std::unique_ptr<WindowApple> create_apple_window();
