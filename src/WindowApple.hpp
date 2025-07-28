#pragma once

#include <memory>

#include "Window.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

class RHIDevice;
namespace NS {
class AutoreleasePool;
}

class WindowApple : public Window {
 public:
  void poll_events() override;
  void init(RHIDevice* device) override;
  void shutdown() override;
  [[nodiscard]] bool should_close() const override;
  GLFWwindow* window_{};
  NS::AutoreleasePool* main_auto_release_pool_{};
};

std::unique_ptr<Window> create_apple_window();
