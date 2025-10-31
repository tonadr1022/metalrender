#include "MetalBuffer.hpp"

#include <Metal/MTLBuffer.hpp>

MetalBuffer::MetalBuffer(const rhi::BufferDesc& desc, MTL::Buffer* buffer, uint32_t bindless_idx)
    : Buffer(desc, bindless_idx), buffer_(buffer) {}

void* MetalBuffer::contents() {
  assert(buffer_);
  return buffer_->contents();
}
