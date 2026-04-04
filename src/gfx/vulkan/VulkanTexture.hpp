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
        is_swapchain_image_(is_swapchain_image) {
    mip_layouts_.assign(desc.mip_levels, VK_IMAGE_LAYOUT_UNDEFINED);
  }
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

  [[nodiscard]] VkImageLayout mip_layout(uint32_t mip) const {
    if (mip >= mip_layouts_.size()) {
      return VK_IMAGE_LAYOUT_UNDEFINED;
    }
    return mip_layouts_[mip];
  }
  void set_mip_layout(uint32_t mip, VkImageLayout layout) {
    if (mip < mip_layouts_.size()) {
      mip_layouts_[mip] = layout;
    }
  }
  void set_all_mip_layouts(VkImageLayout layout) {
    for (auto& l : mip_layouts_) {
      l = layout;
    }
  }
  /// When src sync is omitted, pick an oldLayout for a whole-image barrier; all mips must agree.
  [[nodiscard]] VkImageLayout uniform_mip_layout_or_undefined() const;

  VkImage image_;
  VkImageView default_view_{};
  VmaAllocation allocation_;
  std::vector<VkImageLayout> mip_layouts_;
  bool is_swapchain_image_{false};
};

inline VkImageLayout VulkanTexture::uniform_mip_layout_or_undefined() const {
  if (mip_layouts_.empty()) {
    return VK_IMAGE_LAYOUT_UNDEFINED;
  }
  VkImageLayout u = mip_layouts_[0];
  for (VkImageLayout l : mip_layouts_) {
    if (l != u) {
      return VK_IMAGE_LAYOUT_UNDEFINED;
    }
  }
  return u;
}

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
