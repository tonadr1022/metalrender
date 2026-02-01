#pragma once

#include "core/Config.hpp"
#include "gfx/rhi/Config.hpp"
#include "gfx/rhi/Swapchain.hpp"
#include "vulkan/vulkan_core.h"

namespace TENG_NAMESPACE {

namespace gfx::vk {

class VulkanSwapchain : public rhi::Swapchain {
 public:
  VulkanSwapchain() = default;
  explicit VulkanSwapchain(VkSurfaceKHR surface) : surface_(surface) {}
  ~VulkanSwapchain() = default;

  using SwapchainTextures = std::array<rhi::TextureHandle, k_max_frames_in_flight>;
  SwapchainTextures& get_textures() { return textures_; }

  rhi::TextureHandle get_texture(uint32_t frame_index) override { return textures_[frame_index]; }

  SwapchainTextures textures_;
  VkSurfaceKHR surface_;
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
