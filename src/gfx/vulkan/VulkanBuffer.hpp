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
               VmaAllocation allocation, VmaAllocationCreateFlags alloc_flags, void* mapped_ptr)
      : rhi::Buffer(desc, bindless_idx),
        alloc_flags(alloc_flags),
        buffer_(buffer),
        allocation_(allocation),
        mapped_ptr_(mapped_ptr) {}
  VulkanBuffer() = default;
  ~VulkanBuffer() = default;

  void* contents() override { return mapped_ptr_; }
  [[nodiscard]] const void* contents() const override { return mapped_ptr_; }
  [[nodiscard]] VkBuffer buffer() const { return buffer_; }
  [[nodiscard]] VmaAllocation allocation() const { return allocation_; }

  [[nodiscard]] bool is_cpu_visible() const override {
    return alloc_flags & VMA_ALLOCATION_CREATE_MAPPED_BIT;
  }

  VmaAllocationCreateFlags alloc_flags;
  VkBuffer buffer_{};
  VmaAllocation allocation_{};
  void* mapped_ptr_{};
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
