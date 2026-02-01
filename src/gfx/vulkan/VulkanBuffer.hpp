#pragma once

#include <volk.h>

#include "core/Config.hpp"
#include "gfx/Buffer.hpp"
#include "gfx/vulkan/VMAWrapper.hpp"

namespace TENG_NAMESPACE {

namespace gfx::vk {

class VulkanBuffer final : public rhi::Buffer {
 public:
  VulkanBuffer(const rhi::BufferDesc& desc, uint32_t bindless_idx, VkBuffer buffer,
               VmaAllocation allocation)
      : rhi::Buffer(desc, bindless_idx), buffer_(buffer), allocation_(allocation) {}
  VulkanBuffer() = default;
  ~VulkanBuffer() = default;

  void* contents() override { return nullptr; }
  [[nodiscard]] VkBuffer buffer() const { return buffer_; }
  [[nodiscard]] VmaAllocation allocation() const { return allocation_; }

 private:
  VkBuffer buffer_{};
  VmaAllocation allocation_{};
};

}  // namespace gfx::vk

}  // namespace TENG_NAMESPACE
