#pragma once

#include <Metal/Metal.hpp>

#include "core/Config.hpp"
#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/Swapchain.hpp"

namespace CA {
class MetalLayer;
class MetalDrawable;
}  // namespace CA

namespace TENG_NAMESPACE {

class MetalSwapchain : public rhi::Swapchain {
 public:
  using SwapchainTextures = std::array<rhi::TextureHandleHolder, k_max_frames_in_flight>;

  SwapchainTextures& get_textures() { return textures_; }
  rhi::TextureHandle get_current_texture() override {
    ASSERT(0);
    return textures_[curr_image_index_].handle;
  }

  rhi::TextureHandle get_texture(uint32_t frame_index) override {
    return textures_[frame_index].handle;
  }

  SwapchainTextures textures_;
  CA::MetalLayer* metal_layer_{};
  NS::SharedPtr<CA::MetalDrawable> drawable_;
  uint32_t curr_image_index_{};
  // NS::SharedPtr<
};

}  // namespace TENG_NAMESPACE
