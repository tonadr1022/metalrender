#pragma once

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

  VkImage image_;
  VkImageView default_view_{};
  VmaAllocation allocation_;
  VkImageLayout current_layout_{VK_IMAGE_LAYOUT_UNDEFINED};
  bool is_swapchain_image_{false};
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
