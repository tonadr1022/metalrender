#include "MetalDevice.hpp"

#include <Metal/Metal.hpp>

#include "MetalUtil.hpp"
#include "core/EAssert.hpp"
#include "gfx/GFXTypes.hpp"

void MetalDevice::init() {
  device_ = MTL::CreateSystemDefaultDevice();
  ar_pool_ = NS::AutoreleasePool::alloc()->init();
}

void MetalDevice::shutdown() {
  ar_pool_->release();
  device_->release();
}

rhi::BufferHandle MetalDevice::create_buf(const rhi::BufferDesc& desc) {
  auto options = util::mtl::convert_storage_mode(desc.storage_mode);
  auto* mtl_buf = device_->newBuffer(desc.size, options);
  mtl_buf->retain();
  // if (desc.alloc_gpu_slot) {
  //   gpu_slot = buffer_allocator_.alloc(mtl_buf);
  // }
  return buffer_pool_.alloc(desc, mtl_buf);
}

rhi::TextureHandle MetalDevice::create_tex(const rhi::TextureDesc& desc) {
  MTL::TextureDescriptor* texture_desc = MTL::TextureDescriptor::alloc()->init();
  texture_desc->setWidth(desc.dims.x);
  texture_desc->setHeight(desc.dims.y);
  texture_desc->setDepth(desc.dims.z);
  texture_desc->setPixelFormat(util::mtl::convert_format(desc.format));
  texture_desc->setStorageMode(util::mtl::convert_storage_mode(desc.storage_mode));
  texture_desc->setMipmapLevelCount(desc.mip_levels);
  texture_desc->setArrayLength(desc.array_length);
  // TODO: parameterize this?
  texture_desc->setAllowGPUOptimizedContents(true);
  texture_desc->setUsage(util::mtl::convert_texture_usage(desc.usage));
  auto* tex = device_->newTexture(texture_desc);
  tex->retain();
  texture_desc->release();
  uint32_t idx = rhi::Texture::k_invalid_gpu_slot;
  if (desc.alloc_gpu_slot) {
    idx = texture_index_allocator_.alloc_idx();
  }
  return texture_pool_.alloc(desc, idx, tex);
}

void MetalDevice::destroy(rhi::BufferHandle handle) {
  auto* buf = buffer_pool_.get(handle);
  ASSERT(buf);
  if (!buf) {
    return;
  }

  ASSERT(buf->buffer());
  if (buf->buffer()) {
    buf->buffer()->release();
  }
  // if (buf->gpu_slot() != rhi::Buffer::k_invalid_gpu_slot) {
  //   texture_allocator_.free(buf->gpu_slot());
  // }
  buffer_pool_.destroy(handle);
}

void MetalDevice::destroy(rhi::TextureHandle handle) {
  auto* tex = texture_pool_.get(handle);
  ASSERT(tex);
  if (!tex) {
    return;
  }
  ASSERT(tex->texture());
  if (tex->texture()) {
    tex->texture()->release();
  }
  // if (tex->gpu_slot() != rhi::Texture::k_invalid_gpu_slot) {
  //   texture_allocator_.free(tex->gpu_slot());
  // }
  texture_pool_.destroy(handle);
}

// void MetalDevice::use_bindless_buffer(MTL::RenderCommandEncoder* enc) {
//   enc->useResource(buffer_allocator_.get_buffer(), MTL::ResourceUsageRead);
// }
std::unique_ptr<MetalDevice> create_metal_device() { return std::make_unique<MetalDevice>(); }
