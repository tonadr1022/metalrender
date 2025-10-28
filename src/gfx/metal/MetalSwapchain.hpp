#pragma once

#include "gfx/Config.hpp"
#include "gfx/Swapchain.hpp"

class MetalSwapchain : public rhi::Swapchain {
 public:
  using SwapchainTextures = std::array<rhi::TextureHandleHolder, k_max_frames_in_flight>;

  SwapchainTextures& get_textures() { return textures_; }

  rhi::TextureHandle get_texture(uint32_t frame_index) override {
    return textures_[frame_index].handle;
  }

 private:
  SwapchainTextures textures_{};
};
