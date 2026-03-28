#pragma once

#include "core/Config.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace TENG_NAMESPACE {

class Window;

namespace gfx::rhi {

struct SwapchainDesc {
  Window* window;
  uint32_t width;
  uint32_t height;
  bool vsync;
};

class Swapchain {
 public:
  ~Swapchain() = default;
  virtual TextureHandle get_texture(uint32_t frame_index) = 0;
  virtual TextureHandle get_current_texture() = 0;

  SwapchainDesc desc_;

 private:
};

}  // namespace gfx::rhi

}  // namespace TENG_NAMESPACE
