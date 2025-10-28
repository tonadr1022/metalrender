#pragma once

#include "gfx/RendererTypes.hpp"

namespace rhi {

class Swapchain {
 public:
  ~Swapchain() = default;
  virtual TextureHandle get_texture(uint32_t frame_index) = 0;

 private:
};

}  // namespace rhi
