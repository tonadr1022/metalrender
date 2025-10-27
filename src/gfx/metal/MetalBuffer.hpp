#pragma once

#include "gfx/Buffer.hpp"

namespace MTL {
class Buffer;
}

class MetalBuffer final : public rhi::Buffer {
 public:
  MetalBuffer(const rhi::BufferDesc& desc, MTL::Buffer* buffer);
  MetalBuffer() = default;
  ~MetalBuffer() = default;
  void* contents() override;
  [[nodiscard]] MTL::Buffer* buffer() const { return buffer_; }

 private:
  [[maybe_unused]] MTL::Buffer* buffer_{};
};
