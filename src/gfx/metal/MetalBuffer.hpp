#pragma once

#include "gfx/Buffer.hpp"
#include "gfx/GFXTypes.hpp"

namespace MTL {
class Buffer;
}

class MetalBuffer final : public rhi::Buffer {
 public:
  MetalBuffer(const rhi::BufferDesc& desc, MTL::Buffer* buffer,
              uint32_t bindless_idx = rhi::k_invalid_bindless_idx);
  MetalBuffer() = default;
  ~MetalBuffer() = default;
  void* contents() override;
  [[nodiscard]] MTL::Buffer* buffer() { return buffer_; }

 private:
  [[maybe_unused]] MTL::Buffer* buffer_{};
};
