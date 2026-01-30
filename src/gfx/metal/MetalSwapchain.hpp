#pragma once

#include <Metal/Metal.hpp>

#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/Swapchain.hpp"

namespace CA {
class MetalLayer;
class MetalDrawable;
}  // namespace CA

class MetalSwapchain : public rhi::Swapchain {
 public:
  using SwapchainTextures = std::array<rhi::TextureHandleHolder, k_max_frames_in_flight>;

  SwapchainTextures& get_textures() { return textures_; }

  rhi::TextureHandle get_texture(uint32_t frame_index) override {
    return textures_[frame_index].handle;
  }

  SwapchainTextures textures_{};
  CA::MetalLayer* metal_layer_{nullptr};
  NS::SharedPtr<CA::MetalDrawable> drawable_;
  // NS::SharedPtr<
};
