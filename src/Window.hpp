#pragma once

#include <GLFW/glfw3.h>

#include <functional>
#include <glm/vec2.hpp>
#include <memory>

namespace rhi {
class Device;
}

struct GLFWwindow;

class Window {
 public:
  using KeyCallbackFn = std::function<void(int key, int action, int mods)>;
  using CursorPosCallbackFn = std::function<void(double x_pos, double y_pos)>;
  using FramebufferResizeCallbackFn = std::function<void(int width, int height)>;
  struct InitInfo {
    KeyCallbackFn key_callback_fn;
    CursorPosCallbackFn cursor_pos_callback_fn;
    FramebufferResizeCallbackFn framebuffer_resize_callback_fn;
    int win_dims_x{1280};
    int win_dims_y{720};
    bool floating_window{false};
  };

  virtual void init(InitInfo& init_info);
  void shutdown();
  virtual ~Window() = default;

  [[nodiscard]] bool should_close() const;
  void poll_events();
  glm::uvec2 get_window_size();
  glm::uvec2 get_window_position();
  void set_window_position(glm::ivec2 pos);
  glm::uvec2 get_window_not_framebuffer_size();
  [[nodiscard]] GLFWwindow* get_handle() const { return window_; }
  virtual void set_fullscreen([[maybe_unused]] bool fullscreen) {}
  [[nodiscard]] virtual bool get_fullscreen() const = 0;

 protected:
  KeyCallbackFn key_callback_fn_;
  CursorPosCallbackFn cursor_pos_callback_fn_;
  FramebufferResizeCallbackFn framebuffer_resize_callback_fn_;
  GLFWwindow* window_{};
};

std::unique_ptr<Window> create_platform_window();
