#pragma once

#include <Foundation/NSAutoreleasePool.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

class DeviceMetal;

class WindowApple {
 public:
  void poll_events();
  void init(DeviceMetal* device);
  void shutdown() const;
  [[nodiscard]] bool should_close() const;
  GLFWwindow* window_{};
  NS::AutoreleasePool* main_auto_release_pool_{};
};
