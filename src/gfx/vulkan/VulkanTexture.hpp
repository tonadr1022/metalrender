#pragma once

#include "core/Config.hpp"
#include "gfx/Texture.hpp"
#include "gfx/vulkan/VMAWrapper.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {

class VulkanTexture : public rhi::Texture {
 public:
  VulkanTexture(const rhi::TextureDesc& desc, uint32_t bindless_idx, VkImage image,
                VmaAllocation allocation, VmaAllocationInfo allocation_info)
      : rhi::Texture(desc, bindless_idx),
        image_(image),
        allocation_(allocation),
        allocation_info_(allocation_info) {}
  VulkanTexture() = default;
  ~VulkanTexture() = default;

  [[nodiscard]] VkImage image() const { return image_; }
  [[nodiscard]] VmaAllocation allocation() const { return allocation_; }

  VkImage image_;
  VmaAllocation allocation_;
  VmaAllocationInfo allocation_info_;
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
