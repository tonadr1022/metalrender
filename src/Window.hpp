#pragma once

#include <GLFW/glfw3.h>

#include <functional>
#include <glm/vec2.hpp>

namespace rhi {
class Device;
}

struct GLFWwindow;

class Window {
 public:
  using KeyCallbackFn = std::function<void(int key, int action, int mods)>;
  using CursorPosCallbackFn = std::function<void(double x_pos, double y_pos)>;

  void init(KeyCallbackFn key_callback_fn, CursorPosCallbackFn cursor_pos_callback_fn);
  void shutdown();
  ~Window() = default;

  [[nodiscard]] bool should_close() const;
  void poll_events();
  glm::uvec2 get_window_size();
  [[nodiscard]] GLFWwindow* get_handle() const { return window_; }

 private:
  KeyCallbackFn key_callback_fn_;
  CursorPosCallbackFn cursor_pos_callback_fn_;
  GLFWwindow* window_{};
};
