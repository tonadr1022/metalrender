#include "MetalBuffer.hpp"

#include <Metal/MTLBuffer.hpp>

#include "core/Config.hpp"

namespace TENG_NAMESPACE {

namespace gfx::mtl {

Buffer::Buffer(const rhi::BufferDesc& desc, MTL::Buffer* buffer,
               MTL::ResourceOptions resource_options, uint32_t bindless_idx)
    : rhi::Buffer(desc, bindless_idx), resource_options_(resource_options), buffer_(buffer) {}

void* Buffer::contents() {
  assert(buffer_);
  return buffer_->contents();
}

const void* Buffer::contents() const {
  assert(buffer_);
  return buffer_->contents();
}

}  // namespace gfx::mtl

}  // namespace TENG_NAMESPACE
