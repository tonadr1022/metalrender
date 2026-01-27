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
  auto *layer = CA::MetalLayer::layer();

  if (transparent_allowed) {
    auto *objcLayer = (__bridge CAMetalLayer *)layer;
    objcLayer.opaque = NO;
    objcLayer.opacity = 1.0;
    NSView *ns_view = glfwGetCocoaView(window);
    ns_view.alphaValue = 0.0;
  }

  layer->setDevice(device);
  NSWindow *ns_window = glfwGetCocoaWindow(window);
  ns_window.contentView.layer = (__bridge CAMetalLayer *)layer;
  ns_window.contentView.wantsLayer = YES;
  init_pool->release();
  return layer;
  // return (CA::MetalLayer *)mtl_layer;
}
