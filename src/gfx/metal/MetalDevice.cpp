#include "MetalDevice.hpp"

#include <Metal/Metal.hpp>

namespace {

MTL::PixelFormat convert_format(rhi::TextureFormat format) {
  using TextureFormat = rhi::TextureFormat;
  switch (format) {
    case TextureFormat::R8G8B8A8Srgb:
      return MTL::PixelFormatRGBA8Unorm_sRGB;
    case TextureFormat::R8G8B8A8Unorm:
      return MTL::PixelFormatRGBA8Unorm;
    default:
      assert(0 && "unhandled texture format");
      return MTL::PixelFormatInvalid;
  }
  return MTL::PixelFormatInvalid;
}

MTL::TextureUsage convert_texture_usage(rhi::TextureUsage usage) {
  MTL::TextureUsage mtl_use = 0;

  if (usage & rhi::TextureUsage_RenderTarget) {
    mtl_use |= MTL::TextureUsageRenderTarget;
  }
  if (usage & rhi::TextureUsage_ShaderRead) {
    mtl_use |= MTL::TextureUsageShaderRead;
  }
  if (usage & rhi::TextureUsage_ShaderWrite) {
    mtl_use |= MTL::TextureUsageShaderWrite;
  }
  return mtl_use;
}

MTL::StorageMode convert_storage_mode(rhi::StorageMode mode) {
  using StorageMode = rhi::StorageMode;
  switch (mode) {
    case StorageMode::CPUAndGPU:
    case StorageMode::CPUOnly:
      return MTL::StorageModeShared;
    case StorageMode::GPUOnly:
      return MTL::StorageModePrivate;
    default:
      assert(0 && "invalid storage mode");
      return MTL::StorageModePrivate;
  }
  assert(0 && "unreachable");
  return MTL::StorageModePrivate;
}

struct MetalTexture {
  MTL::Texture* texture{};
};

}  // namespace

MetalDevice::MetalDevice() : mtl_device_(MTL::CreateSystemDefaultDevice()) {
  main_queue_ = mtl_device_->newCommandQueue();
}

void MetalDevice::shutdown() { mtl_device_->release(); }

std::unique_ptr<MetalDevice> create_metal_device() { return std::make_unique<MetalDevice>(); }

rhi::Texture MetalDevice::create_texture(const rhi::TextureDesc& desc) {
  MTL::TextureDescriptor* texture_desc = MTL::TextureDescriptor::alloc()->init();
  texture_desc->setWidth(desc.dims.x);
  texture_desc->setHeight(desc.dims.y);
  texture_desc->setDepth(desc.dims.z);
  texture_desc->setPixelFormat(convert_format(desc.format));
  texture_desc->setStorageMode(convert_storage_mode(desc.storage_mode));
  texture_desc->setMipmapLevelCount(desc.mip_levels);
  texture_desc->setArrayLength(desc.array_length);
  // TODO: reconsider this?
  texture_desc->setAllowGPUOptimizedContents(true);
  texture_desc->setUsage(convert_texture_usage(desc.usage));
  MTL::Texture* tex = mtl_device_->newTexture(texture_desc);
  texture_desc->release();

  rhi::Texture new_tex{desc};
  auto mtl_tex = std::make_shared<MetalTexture>();
  mtl_tex->texture = tex;

  new_tex.internal_state = mtl_tex;

  return new_tex;
}

void MetalDevice::generate_mipmaps(std::span<rhi::Texture> textures) {
  MTL::CommandBuffer* buf = main_queue_->commandBuffer();
  MTL::BlitCommandEncoder* blit_enc = buf->blitCommandEncoder();
  for (auto& tex : textures) {
    auto mtl_tex = std::static_pointer_cast<MetalTexture>(tex.internal_state);
    blit_enc->generateMipmaps(mtl_tex->texture);
  }
  blit_enc->endEncoding();
  buf->commit();
}
