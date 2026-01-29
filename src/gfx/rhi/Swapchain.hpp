#pragma once

#include "gfx/rhi/GFXTypes.hpp"

class Window;

namespace rhi {

struct SwapchainDesc {
  Window* window;
  bool vsync;
  uint32_t width;
  uint32_t height;
};

class Swapchain {
 public:
  ~Swapchain() = default;
  virtual TextureHandle get_texture(uint32_t frame_index) = 0;

  SwapchainDesc desc_;

 private:
};

}  // namespace rhi
