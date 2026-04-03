#pragma once

#include <vulkan/vulkan_core.h>

#include <vector>

#include "core/Config.hpp"
#include "gfx/rhi/Texture.hpp"
#include "gfx/vulkan/VMAWrapper.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {

class VulkanTexture : public rhi::Texture {
 public:
  VulkanTexture(const rhi::TextureDesc& desc, uint32_t bindless_idx, VkImage image,
                VmaAllocation allocation, bool is_swapchain_image)
      : rhi::Texture(desc, bindless_idx),
        image_(image),
        allocation_(allocation),
        is_swapchain_image_(is_swapchain_image) {}
  VulkanTexture() = default;
  ~VulkanTexture() = default;

  [[nodiscard]] VkImage image() const { return image_; }
  [[nodiscard]] VmaAllocation allocation() const {
    ASSERT(!is_swapchain_image_);
    return allocation_;
  }
  rhi::TextureDesc& desc() { return desc_; }
  uint32_t& bindless_idx() { return bindless_idx_; }
  [[nodiscard]] uint32_t raw_bindless_idx() const { return bindless_idx_; }

  // Indices are stable handles; destroyed slots keep view == VK_NULL_HANDLE (Metal-style).
  struct TexView {
    VkImageView view{};
    uint32_t bindless_idx{rhi::k_invalid_bindless_idx};
  };
  std::vector<TexView> tex_views;

  VkImage image_;
  VkImageView default_view_{};
  VmaAllocation allocation_;
  VkImageLayout current_layout_{VK_IMAGE_LAYOUT_UNDEFINED};
  bool is_swapchain_image_{false};
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
