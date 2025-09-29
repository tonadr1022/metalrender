#include "MetalBuffer.hpp"

#include <Metal/MTLBuffer.hpp>

MetalBuffer::MetalBuffer(const rhi::BufferDesc& desc, uint32_t gpu_slot, MTL::Buffer* buffer)
    : Buffer(desc, gpu_slot), buffer_(buffer) {}

void* MetalBuffer::contents() {
  assert(buffer_);
  return buffer_->contents();
}
