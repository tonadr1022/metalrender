#include "MetalDevice.hpp"

#include <Metal/Metal.hpp>

#include "MetalUtil.hpp"
#include "gfx/GFXTypes.hpp"

void MetalDevice::init() {
  device_ = MTL::CreateSystemDefaultDevice();
  ar_pool_ = NS::AutoreleasePool::alloc()->init();
}
void MetalDevice::shutdown() {
  ar_pool_->release();
  device_->release();
}
MTL::Texture* MetalDevice::create_texture(const TextureDesc& desc) {
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
  texture_desc->release();
  return tex;
}

std::unique_ptr<MetalDevice> create_metal_device() { return std::make_unique<MetalDevice>(); }
