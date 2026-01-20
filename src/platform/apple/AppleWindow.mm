#include "AppleWindow.hpp"
#include "core/Logger.hpp"

#import <Cocoa/Cocoa.h>
#include <GLFW/glfw3.h>
#include <sys/wait.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#include <dispatch/dispatch.h>

void AppleWindow::init(KeyCallbackFn key_callback_fn,
                       CursorPosCallbackFn cursor_pos_callback_fn,
                       bool transparent_window, int win_dims_x,
                       int win_dims_y) {
  Window::init(key_callback_fn, cursor_pos_callback_fn, transparent_window,
               win_dims_x, win_dims_y);
}

void AppleWindow::set_fullscreen([[maybe_unused]] bool fullscreen) {
  fullscreen_ = fullscreen;
  if (!window_) {
    return;
  }

  auto nsWindow = (__bridge NSWindow *)glfwGetCocoaWindow(window_);
  if (!nsWindow) {
    return;
  }

  dispatch_async(dispatch_get_main_queue(), ^{
    [nsWindow toggleFullScreen:nil];
  });
}

[[nodiscard]] bool AppleWindow::get_fullscreen() const {
  auto ns_window = (__bridge NSWindow *)glfwGetCocoaWindow(window_);
  if (!ns_window) {
    exit(1);
  }
  return [ns_window styleMask] & NSWindowStyleMaskFullScreen;
}
