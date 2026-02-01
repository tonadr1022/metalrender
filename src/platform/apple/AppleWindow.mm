#include "AppleWindow.hpp"
#include "core/Logger.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <QuartzCore/QuartzCore.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#include <dispatch/dispatch.h>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

void AppleWindow::init(InitInfo &init_info) {
  Window::init(init_info);
  NSView *ns_view = glfwGetCocoaView(window_);
  ns_view.alphaValue = 0.0;
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

void set_layer_for_window(GLFWwindow *window, CA::MetalLayer *layer) {
  NSWindow *ns_window = glfwGetCocoaWindow(window);
  ns_window.contentView.layer = (__bridge CAMetalLayer *)layer;
  ns_window.contentView.wantsLayer = YES;
}

} // namespace TENG_NAMESPACE
