#pragma once

#include "gfx/Buffer.hpp"

class VulkanBuffer final : public rhi::Buffer {
 public:
  VulkanBuffer(const rhi::BufferDesc& desc, uint32_t bindless_idx)
      : rhi::Buffer(desc, bindless_idx) {}
  VulkanBuffer() = default;
  ~VulkanBuffer() = default;

  void* contents() override { return nullptr; }
};
