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

  void set_vsync(bool vsync) override;
  bool get_vsync() const override;
  void init(rhi::Device* device, KeyCallbackFn key_callback_fn,
            CursorPosCallbackFn cursor_pos_callback_fn) override;
  void shutdown() override;
  [[nodiscard]] bool should_close() const override;
  [[nodiscard]] GLFWwindow* get_handle() const { return window_; }
  glm::uvec2 get_window_size() override;

  // TODO: lol
  CA::MetalLayer* metal_layer_{};

 private:
  KeyCallbackFn key_callback_fn_;
  CursorPosCallbackFn cursor_pos_callback_fn_;
  GLFWwindow* window_{};
};

std::unique_ptr<WindowApple> create_apple_window();
