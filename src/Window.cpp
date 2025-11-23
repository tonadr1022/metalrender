#include "Window.hpp"

#include "core/Logger.hpp"

void Window::init(KeyCallbackFn key_callback_fn, CursorPosCallbackFn cursor_pos_callback_fn,
                  bool transparent_window) {
  this->key_callback_fn_ = std::move(key_callback_fn);
  this->cursor_pos_callback_fn_ = std::move(cursor_pos_callback_fn);
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  if (transparent_window) {
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
  }

  int w = 1024;
  int h = 1024;
  GLFWmonitor* monitor = glfwGetPrimaryMonitor();
  const GLFWvidmode* mode = glfwGetVideoMode(monitor);
  if (mode) {
    w = mode->width;
    h = mode->height;
  }
  window_ = glfwCreateWindow(w, h, "Memes", nullptr, nullptr);
  if (!window_) {
    LCRITICAL("Failed to create glfw window");
    glfwTerminate();
    exit(EXIT_FAILURE);
  }

  glfwSetWindowUserPointer(window_, this);
  glfwSetKeyCallback(window_, [](GLFWwindow* window, int key, [[maybe_unused]] int scancode,
                                 int action, int mods) {
    auto* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    win->key_callback_fn_(key, action, mods);
  });
  glfwSetCursorPosCallback(window_, [](GLFWwindow* window, double xpos, double ypos) {
    auto* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    win->cursor_pos_callback_fn_(xpos, ypos);
  });
}

void Window::shutdown() { glfwTerminate(); }
void Window::poll_events() { glfwPollEvents(); }

bool Window::should_close() const { return glfwWindowShouldClose(window_); }

glm::uvec2 Window::get_window_size() {
  int x, y;
  glfwGetFramebufferSize(window_, &x, &y);
  return {x, y};
}

glm::uvec2 Window::get_window_not_framebuffer_size() {
  int x, y;
  glfwGetWindowSize(window_, &x, &y);
  return {x, y};
}
