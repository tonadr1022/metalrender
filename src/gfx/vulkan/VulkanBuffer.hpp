#pragma once

#include <volk.h>

#include "core/Config.hpp"
#include "gfx/rhi/Buffer.hpp"
#include "gfx/vulkan/VMAWrapper.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {

class VulkanBuffer final : public rhi::Buffer {
 public:
  VulkanBuffer(const rhi::BufferDesc& desc, uint32_t bindless_idx, VkBuffer buffer,
               VmaAllocation allocation, void* mapped_ptr)
      : rhi::Buffer(desc, bindless_idx),
        buffer_(buffer),
        allocation_(allocation),
        mapped_ptr_(mapped_ptr) {}
  VulkanBuffer() = default;
  ~VulkanBuffer() = default;

  void* contents() override { return mapped_ptr_; }
  [[nodiscard]] VkBuffer buffer() const { return buffer_; }
  [[nodiscard]] VmaAllocation allocation() const { return allocation_; }

  VkBuffer buffer_{};
  VmaAllocation allocation_{};
  void* mapped_ptr_{};
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
