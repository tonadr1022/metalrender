#pragma once

#include <string>

#include "Foundation/NSString.hpp"
#include "Metal/MTLPixelFormat.hpp"
#include "Metal/MTLResource.hpp"
#include "Metal/MTLTexture.hpp"
#include "gfx/GFXTypes.hpp"

namespace NS {
class Error;
}  // namespace NS

namespace util::mtl {

void print_err(NS::Error *err);

inline NS::String *string(const char *v) { return NS::String::string(v, NS::ASCIIStringEncoding); }
inline NS::String *string(const std::string &v) { return string(v.c_str()); }

inline MTL::TextureUsage convert_texture_usage(rhi::TextureUsage usage) {
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

inline MTL::StorageMode convert_storage_mode(rhi::StorageMode mode) {
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
      assert(0 && "invalid storage mode");
      return MTL::StorageModePrivate;
  }
  assert(0 && "unreachable");
  return MTL::StorageModePrivate;
}

inline MTL::PixelFormat convert_format(rhi::TextureFormat format) {
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
    default:
      assert(0 && "unhandled texture format");
      return MTL::PixelFormatInvalid;
  }
  return MTL::PixelFormatInvalid;
}
}  // namespace util::mtl
