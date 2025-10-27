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

namespace mtl::util {

void print_err(NS::Error *err);

inline const char *get_err_string(NS::Error *err) {
  return err->localizedDescription()->cString(NS::ASCIIStringEncoding);
}

inline NS::String *string(const char *v) { return NS::String::string(v, NS::ASCIIStringEncoding); }
inline NS::String *string(const std::string &v) { return string(v.c_str()); }

MTL::TextureUsage convert_texture_usage(rhi::TextureUsage usage);

MTL::StorageMode convert_storage_mode(rhi::StorageMode mode);

MTL::PixelFormat convert_format(rhi::TextureFormat format);

}  // namespace mtl::util
