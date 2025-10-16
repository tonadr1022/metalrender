#include "WindowApple.hpp"

#include <QuartzCore/QuartzCore.h>

#include <Metal/Metal.hpp>
#include <print>

#include "GLFW/glfw3.h"
#include "gfx/metal/MetalDevice.hpp"

void WindowApple::init(rhi::Device* device, KeyCallbackFn key_callback_fn,
                       CursorPosCallbackFn cursor_pos_callback_fn) {
  this->key_callback_fn_ = key_callback_fn;
  this->cursor_pos_callback_fn_ = cursor_pos_callback_fn;
  auto* init_pool = NS::AutoreleasePool::alloc()->init();
  glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window_ = glfwCreateWindow(1024, 1024, "Metal Engine", nullptr, nullptr);
  if (!window_) {
    std::println("Failed to create glfw window");
    glfwTerminate();
    exit(EXIT_FAILURE);
  }

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

  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  CGFloat bg_color[] = {0.0, 0.0, 1.0, 1.0};
  CGColorRef bg_color_ref = CGColorCreate(color_space, bg_color);
  mtl_layer.backgroundColor = bg_color_ref;

  CGColorRelease(bg_color_ref);
  CGColorSpaceRelease(color_space);
  [ns_view setLayer:mtl_layer];
  [ns_view setWantsLayer:YES];
  metal_layer_ = (CA::MetalLayer*)mtl_layer;
  init_pool->release();

  glfwSetWindowUserPointer(window_, this);
  glfwSetKeyCallback(window_, [](GLFWwindow* window, int key, [[maybe_unused]] int scancode,
                                 int action, int mods) {
    auto* win = reinterpret_cast<WindowApple*>(glfwGetWindowUserPointer(window));
    win->key_callback_fn_(key, action, mods);
  });
  glfwSetCursorPosCallback(window_, [](GLFWwindow* window, double xpos, double ypos) {
    auto* win = reinterpret_cast<WindowApple*>(glfwGetWindowUserPointer(window));
    win->cursor_pos_callback_fn_(xpos, ypos);
  });
}

void WindowApple::shutdown() { glfwTerminate(); }
void WindowApple::poll_events() { glfwPollEvents(); }

void WindowApple::set_vsync(bool vsync) {
  [(CAMetalLayer*)metal_layer_ setDisplaySyncEnabled:vsync];
}

bool WindowApple::get_vsync() const { return [(CAMetalLayer*)metal_layer_ displaySyncEnabled]; }

bool WindowApple::should_close() const { return glfwWindowShouldClose(window_); }

glm::uvec2 WindowApple::get_window_size() {
  int x, y;
  glfwGetFramebufferSize(window_, &x, &y);
  return {x, y};
}

std::unique_ptr<WindowApple> create_apple_window() { return std::make_unique<WindowApple>(); }
