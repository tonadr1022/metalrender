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

namespace NS {
class AutoreleasePool;
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
  GLFWwindow* window_{};
  NS::AutoreleasePool* main_auto_release_pool_{};
  CA::MetalLayer* metal_layer_{};
};

std::unique_ptr<WindowApple> create_apple_window();
