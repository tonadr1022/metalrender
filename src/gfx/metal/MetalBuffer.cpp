#include "MetalBuffer.hpp"

#include <Metal/MTLBuffer.hpp>

MetalBuffer::MetalBuffer(const rhi::BufferDesc& desc, MTL::Buffer* buffer)
    : Buffer(desc), buffer_(buffer) {}

void* MetalBuffer::contents() {
  assert(buffer_);
  return buffer_->contents();
}
