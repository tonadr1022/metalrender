#pragma once

#include "Foundation/NSString.hpp"
#include "Metal/MTLPixelFormat.hpp"
#include "Metal/MTLResource.hpp"
#include "gfx/GFXTypes.hpp"

namespace NS {
class Error;
}  // namespace NS

namespace util::mtl {

void print_err(NS::Error *err);

inline NS::String *string(const char *v) { return NS::String::string(v, NS::ASCIIStringEncoding); }

inline MTL::StorageMode convert_storage_mode(StorageMode mode) {
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

inline MTL::PixelFormat convert_format(TextureFormat format) {
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
}  // namespace util::mtl
