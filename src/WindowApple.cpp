#include "WindowApple.hpp"

#include <QuartzCore/QuartzCore.h>

#include <Metal/Metal.hpp>
#include <print>

#include "GLFW/glfw3.h"
#include "gfx/DeviceMetal.hpp"

void WindowApple::init(RHIDevice* device) {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window_ = glfwCreateWindow(800, 600, "Metal Engine", nullptr, nullptr);
  if (!window_) {
    std::println("Failed to create glfw window");
    glfwTerminate();
    exit(EXIT_FAILURE);
  }
  main_auto_release_pool_ = NS::AutoreleasePool::alloc()->init();

  NSView* ns_view = glfwGetCocoaView(window_);
  NSWindow* ns_window = glfwGetCocoaWindow(window_);
  [ns_view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  CAMetalLayer* mtl_layer = [CAMetalLayer layer];

  mtl_layer.contentsScale = [ns_window backingScaleFactor];
  mtl_layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
  mtl_layer.frame = ns_view.bounds;
  mtl_layer.magnificationFilter = kCAFilterNearest;
  mtl_layer.minificationFilter = kCAFilterNearest;
  mtl_layer.device = (__bridge id<MTLDevice>)device->get_native_device();
  mtl_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  // [mtl_layer setFramebufferOnly:NO];
  // [mtl_layer removeAllAnimations];

  CGFloat bg_color[] = {0.0, 0.0, 1.0, 1.0};
  mtl_layer.backgroundColor = CGColorCreate(CGColorSpaceCreateDeviceRGB(), bg_color);
  [ns_view setLayer:mtl_layer];
  [ns_view setWantsLayer:YES];
}

void WindowApple::shutdown() {
  main_auto_release_pool_->release();
  glfwTerminate();
}
void WindowApple::poll_events() { glfwPollEvents(); }

bool WindowApple::should_close() const { return glfwWindowShouldClose(window_); }

std::unique_ptr<Window> create_apple_window() { return std::make_unique<WindowApple>(); }
