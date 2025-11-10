#include "Window.hpp"

#include "core/Logger.hpp"

void Window::init(KeyCallbackFn key_callback_fn, CursorPosCallbackFn cursor_pos_callback_fn) {
  this->key_callback_fn_ = std::move(key_callback_fn);
  this->cursor_pos_callback_fn_ = std::move(cursor_pos_callback_fn);
  glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window_ = glfwCreateWindow(1024, 1024, "Metal Engine", nullptr, nullptr);
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
