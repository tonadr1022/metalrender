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
  VkSurfaceKHR surface_{};
  VkSwapchainKHR swapchain_{};
  // queue submissions writing to swapchain image must wait for this semaphore
  VkSemaphore acquire_semaphores_[k_max_frames_in_flight]{};
  // when rendering is done, signal this semaphore to be waited on during present
  VkSemaphore ready_to_present_semaphores_[k_max_frames_in_flight]{};
  uint32_t acquire_semaphore_idx_{};
  uint32_t swapchain_tex_count_{};
  uint32_t curr_img_idx_{};
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
