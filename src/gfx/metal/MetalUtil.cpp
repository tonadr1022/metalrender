#include "MetalUtil.hpp"

#include <Metal/Metal.hpp>
#include <cassert>

#include "core/Logger.hpp"

namespace mtl::util {

void print_err(NS::Error* err) {
  assert(err);
  LINFO("{}", err->localizedDescription()->cString(NS::ASCIIStringEncoding));
}

}  // namespace mtl::util
MTL::PixelFormat mtl::util::convert_format(rhi::TextureFormat format) {
  using namespace rhi;
  switch (format) {
    case TextureFormat::R8G8B8A8Srgb:
      return MTL::PixelFormatRGBA8Unorm_sRGB;
    case TextureFormat::R8G8B8A8Unorm:
      return MTL::PixelFormatRGBA8Unorm;
    case TextureFormat::D32float:
      return MTL::PixelFormatDepth32Float;
    case TextureFormat::R32float:
      return MTL::PixelFormatR32Float;
    case TextureFormat::B8G8R8A8Unorm:
      return MTL::PixelFormatBGRA8Unorm;
    default:
      ASSERT(0 && "unhandled texture format");
      return MTL::PixelFormatInvalid;
  }
  return MTL::PixelFormatInvalid;
}
MTL::StorageMode mtl::util::convert_storage_mode(rhi::StorageMode mode) {
  using namespace rhi;
  switch (mode) {
    case StorageMode::CPUAndGPU:
    case StorageMode::CPUOnly:
      return MTL::StorageModeShared;
    case StorageMode::GPUOnly:
      return MTL::StorageModePrivate;
    case StorageMode::Default:
      return MTL::StorageModeShared;
    default:
      ASSERT(0 && "invalid storage mode");
      return MTL::StorageModePrivate;
  }
  ASSERT(0 && "unreachable");
  return MTL::StorageModePrivate;
}
MTL::TextureUsage mtl::util::convert_texture_usage(rhi::TextureUsage usage) {
  using namespace rhi;
  MTL::TextureUsage result{};

  if (usage & TextureUsageShaderRead) {
    result |= MTL::TextureUsageShaderRead;
  }
  if (usage & TextureUsageRenderTarget) {
    result |= MTL::TextureUsageRenderTarget;
  }
  if (usage & TextureUsageShaderWrite) {
    result |= MTL::TextureUsageShaderWrite;
  }

  return result;
}
