#include <QuartzCore/QuartzCore.h>

#include <QuartzCore/CAMetalLayer.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#include <Metal/Metal.hpp>

#include "MetalDevice.hpp"

CA::MetalLayer *init_metal_window(GLFWwindow *window, MTL::Device *device,
                                  bool transparent_allowed) {
  auto *init_pool = NS::AutoreleasePool::alloc()->init();
  NSView *ns_view = glfwGetCocoaView(window);
  NSWindow *ns_window = glfwGetCocoaWindow(window);
  [ns_view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

  CAMetalLayer *mtl_layer = [CAMetalLayer layer];
  if (transparent_allowed) {
    [ns_view setAlphaValue:0.0];
    [mtl_layer setOpaque:FALSE];
    [mtl_layer setOpacity:(1.0)];
  }

  mtl_layer.contentsScale = [ns_window backingScaleFactor];
  mtl_layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
  mtl_layer.frame = ns_view.bounds;
  mtl_layer.magnificationFilter = kCAFilterNearest;
  mtl_layer.minificationFilter = kCAFilterNearest;
  mtl_layer.device = (__bridge id<MTLDevice>)device;
  mtl_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;

  CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
  CGFloat bg_color[] = {0.0, 0.0, 0.0, 0.0};
  CGColorRef bg_color_ref = CGColorCreate(color_space, bg_color);
  mtl_layer.backgroundColor = bg_color_ref;

  CGColorRelease(bg_color_ref);
  CGColorSpaceRelease(color_space);
  [ns_view setLayer:mtl_layer];
  [ns_view setWantsLayer:YES];
  init_pool->release();
  return (CA::MetalLayer *)mtl_layer;
}
