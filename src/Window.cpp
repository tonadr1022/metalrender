#include "Window.hpp"

#include <tracy/Tracy.hpp>

#include "core/Logger.hpp"

void Window::init(InitInfo& init_info) {
  ZoneScoped;
  this->key_callback_fn_ = std::move(init_info.key_callback_fn);
  this->cursor_pos_callback_fn_ = std::move(init_info.cursor_pos_callback_fn);
  this->framebuffer_resize_callback_fn_ = std::move(init_info.framebuffer_resize_callback_fn);
  if (!glfwInit()) {
    LCRITICAL("Failed to initialize glfw");
    exit(1);
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
  if (init_info.floating_window) {
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
  }

  int w = init_info.win_dims_x;
  int h = init_info.win_dims_y;
  if (w <= 0 || h <= 0) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (mode) {
      LINFO("resizing to monitor size");
      w = mode->width;
      h = mode->height;
    }
  }

  window_ = glfwCreateWindow(w, h, "Memes", nullptr, nullptr);
  if (!window_) {
    LCRITICAL("Failed to create glfw window");
    glfwTerminate();
    exit(1);
  }

  glfwSetWindowUserPointer(window_, this);
  glfwSetKeyCallback(window_, [](GLFWwindow* window, int key, [[maybe_unused]] int scancode,
                                 int action, int mods) {
    auto* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->key_callback_fn_) {
      win->key_callback_fn_(key, action, mods);
    }
  });
  glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* window, int width, int height) {
    auto* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->framebuffer_resize_callback_fn_) {
      win->framebuffer_resize_callback_fn_(width, height);
    }
  });
  glfwSetCursorPosCallback(window_, [](GLFWwindow* window, double xpos, double ypos) {
    auto* win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win->cursor_pos_callback_fn_) {
      win->cursor_pos_callback_fn_(xpos, ypos);
    }
  });
}

void Window::shutdown() { glfwTerminate(); }
void Window::poll_events() {
  ZoneScoped;
  glfwPollEvents();
}

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

glm::uvec2 Window::get_window_position() {
  int x, y;
  glfwGetWindowPos(window_, &x, &y);
  return {x, y};
}

void Window::set_window_position(glm::ivec2 pos) { glfwSetWindowPos(window_, pos.x, pos.y); }
