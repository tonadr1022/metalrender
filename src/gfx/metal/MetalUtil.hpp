#pragma once

#include <Metal/Metal.hpp>
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

MTL::TextureUsage convert(rhi::TextureUsage usage);
MTL::StorageMode convert(rhi::StorageMode mode);
MTL::PixelFormat convert(rhi::TextureFormat format);
MTL::CullMode convert(rhi::CullMode mode);
MTL::Winding convert(rhi::WindOrder wind_order);
MTL::CompareFunction convert(rhi::CompareOp op);
MTL::LoadAction convert(rhi::LoadOp op);
MTL::StoreAction convert(rhi::StoreOp op);
MTL::PrimitiveType convert(rhi::PrimitiveTopology top);

}  // namespace mtl::util
