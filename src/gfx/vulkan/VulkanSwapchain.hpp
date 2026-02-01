#pragma once

#include "gfx/Config.hpp"
#include "gfx/Swapchain.hpp"

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {

class VulkanSwapchain : public rhi::Swapchain {
 public:
  VulkanSwapchain() = default;
  ~VulkanSwapchain() = default;

  using SwapchainTextures = std::array<rhi::TextureHandleHolder, k_max_frames_in_flight>;
  SwapchainTextures& get_textures() { return textures_; }

  rhi::TextureHandle get_texture(uint32_t frame_index) override {
    return textures_[frame_index].handle;
  }

 private:
  SwapchainTextures textures_{};
};

}  // namespace gfx::vk

} // namespace TENG_NAMESPACE
