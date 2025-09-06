#pragma once

#include "gfx/Buffer.hpp"

namespace MTL {
class Buffer;
}

class MetalBuffer : public rhi::Buffer {
 public:
  explicit MetalBuffer(const rhi::BufferDesc& desc);

 private:
  [[maybe_unused]] MTL::Buffer* buffer_;
};
