#include "MetalBuffer.hpp"

#include <Metal/MTLBuffer.hpp>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

MetalBuffer::MetalBuffer(const rhi::BufferDesc& desc, MTL::Buffer* buffer, uint32_t bindless_idx)
    : Buffer(desc, bindless_idx), buffer_(buffer) {}

void* MetalBuffer::contents() {
  assert(buffer_);
  return buffer_->contents();
}

} // namespace TENG_NAMESPACE
