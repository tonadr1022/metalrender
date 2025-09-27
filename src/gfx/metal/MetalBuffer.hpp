#pragma once

#include "gfx/Buffer.hpp"

namespace MTL {
class Buffer;
}

class MetalBuffer : public rhi::Buffer {
 public:
  MetalBuffer(const rhi::BufferDesc& desc, MTL::Buffer* buffer);
  MetalBuffer() = default;
  void* contents() override;

  [[maybe_unused]] MTL::Buffer* buffer_{};
};
