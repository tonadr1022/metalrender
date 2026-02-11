#include "MetalBuffer.hpp"

#include <Metal/MTLBuffer.hpp>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

MetalBuffer::MetalBuffer(const rhi::BufferDesc& desc, MTL::Buffer* buffer,
                         MTL::ResourceOptions resource_options, uint32_t bindless_idx)
    : Buffer(desc, bindless_idx), resource_options_(resource_options), buffer_(buffer) {}

void* MetalBuffer::contents() {
  assert(buffer_);
  return buffer_->contents();
}

const void* MetalBuffer::contents() const {
  assert(buffer_);
  return buffer_->contents();
}

}  // namespace TENG_NAMESPACE
