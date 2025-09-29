#pragma once

#include "gfx/Buffer.hpp"

namespace MTL {
class Buffer;
}

class MetalBuffer : public rhi::Buffer {
 public:
  MetalBuffer(const rhi::BufferDesc& desc, uint32_t gpu_slot, MTL::Buffer* buffer);
  MetalBuffer() = default;
  void* contents() override;
  [[nodiscard]] MTL::Buffer* buffer() const { return buffer_; }

 private:
  [[maybe_unused]] MTL::Buffer* buffer_{};
};
