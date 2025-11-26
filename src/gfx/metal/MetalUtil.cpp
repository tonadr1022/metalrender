#include "MetalUtil.hpp"

#include <Metal/Metal.hpp>
#include <cassert>

#include "core/Logger.hpp"
#include "gfx/GFXTypes.hpp"

namespace mtl::util {

void print_err(NS::Error* err) {
  assert(err);
  LINFO("{}", err->localizedDescription()->cString(NS::ASCIIStringEncoding));
}

MTL::CullMode convert(rhi::CullMode mode) {
  switch (mode) {
    case rhi::CullMode::Back:
      return MTL::CullModeBack;
    case rhi::CullMode::Front:
      return MTL::CullModeFront;
    case rhi::CullMode::None:
      return MTL::CullModeNone;
  }
}

MTL::Winding convert(rhi::WindOrder wind_order) {
  switch (wind_order) {
    case rhi::WindOrder::Clockwise:
      return MTL::WindingClockwise;
    case rhi::WindOrder::CounterClockwise:
      return MTL::WindingCounterClockwise;
  }
}

MTL::CompareFunction convert(rhi::CompareOp op) {
  switch (op) {
    case rhi::CompareOp::Less:
      return MTL::CompareFunctionLess;
    case rhi::CompareOp::Greater:
      return MTL::CompareFunctionGreater;
    case rhi::CompareOp::LessOrEqual:
      return MTL::CompareFunctionLessEqual;
    case rhi::CompareOp::GreaterOrEqual:
      return MTL::CompareFunctionGreaterEqual;
    case rhi::CompareOp::Always:
      return MTL::CompareFunctionAlways;
    case rhi::CompareOp::Never:
      return MTL::CompareFunctionNever;
    case rhi::CompareOp::NotEqual:
      return MTL::CompareFunctionNotEqual;
    case rhi::CompareOp::Equal:
      return MTL::CompareFunctionEqual;
  }
}
MTL::LoadAction convert(rhi::LoadOp op) {
  switch (op) {
    case rhi::LoadOp::Load:
      return MTL::LoadActionLoad;
    case rhi::LoadOp::Clear:
      return MTL::LoadActionClear;
    default:
      return MTL::LoadActionDontCare;
  }
}
MTL::StoreAction convert(rhi::StoreOp op) {
  switch (op) {
    case rhi::StoreOp::Store:
      return MTL::StoreActionStore;
    default:
      return MTL::StoreActionDontCare;
  }
}
MTL::PrimitiveType convert(rhi::PrimitiveTopology top) {
  switch (top) {
    case rhi::PrimitiveTopology::TriangleList:
      return MTL::PrimitiveTypeTriangle;
    case rhi::PrimitiveTopology::TriangleStrip:
      return MTL::PrimitiveTypeTriangleStrip;
    case rhi::PrimitiveTopology::LineList:
      return MTL::PrimitiveTypeLine;
    case rhi::PrimitiveTopology::LineStrip:
      return MTL::PrimitiveTypeLineStrip;
    default:
      ALWAYS_ASSERT(0 && "unsupported primitive topology");
      return MTL::PrimitiveTypeTriangle;
  }
}

MTL::StorageMode convert_storage_mode(rhi::StorageMode mode) {
  using namespace rhi;
  switch (mode) {
    case StorageMode::CPUAndGPU:
    case StorageMode::Default:
      return MTL::StorageModeShared;
    case StorageMode::GPUOnly:
      return MTL::StorageModePrivate;
    default:
      ASSERT(0 && "invalid storage mode");
      return MTL::StorageModePrivate;
  }
  ASSERT(0 && "unreachable");
  return MTL::StorageModePrivate;
}

}  // namespace mtl::util
MTL::PixelFormat mtl::util::convert(rhi::TextureFormat format) {
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

MTL::ResourceOptions mtl::util::convert(rhi::StorageMode mode) {
  using namespace rhi;
  switch (mode) {
    case StorageMode::CPUAndGPU:
    case StorageMode::Default:
      return MTL::ResourceStorageModeShared;
    case StorageMode::GPUOnly:
      return MTL::ResourceStorageModePrivate;
    default:
      ASSERT(0 && "invalid storage mode");
      return MTL::ResourceStorageModePrivate;
  }
  ASSERT(0 && "unreachable");
  return MTL::StorageModePrivate;
}

MTL::TextureUsage mtl::util::convert(rhi::TextureUsage usage) {
  using namespace rhi;
  MTL::TextureUsage result{};

  if (usage & TextureUsageSample || usage & rhi::TextureUsageStorage) {
    result |= MTL::TextureUsageShaderRead;
  }
  if (usage & rhi::TextureUsageColorAttachment || usage & rhi::TextureUsageDepthStencilAttachment) {
    result |= MTL::TextureUsageRenderTarget;
  }
  if (usage & TextureUsageShaderWrite) {
    result |= MTL::TextureUsageShaderWrite;
  }

  return result;
}
