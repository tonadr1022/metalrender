#include "MetalUtil.hpp"

#include <Metal/Metal.hpp>
#include <cassert>

#include "core/Logger.hpp"
#include "gfx/rhi/GFXTypes.hpp"

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

MTL::ResourceOptions convert_resource_storage_mode(rhi::StorageMode mode) {
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
  return MTL::ResourceStorageModePrivate;
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

rhi::TextureFormat convert(MTL::PixelFormat format) {
  using namespace rhi;
  switch (format) {
    case MTL::PixelFormatRGBA8Unorm_sRGB:
      return TextureFormat::R8G8B8A8Srgb;
    case MTL::PixelFormatRGBA8Unorm:
      return TextureFormat::R8G8B8A8Unorm;
    case MTL::PixelFormatDepth32Float:
      return TextureFormat::D32float;
    case MTL::PixelFormatR32Float:
      return TextureFormat::R32float;
    case MTL::PixelFormatBGRA8Unorm_sRGB:
      return TextureFormat::B8G8R8A8Srgb;
    case MTL::PixelFormatBGRA8Unorm:
      return TextureFormat::B8G8R8A8Unorm;
    case MTL::PixelFormatASTC_4x4_sRGB:
      return TextureFormat::ASTC4x4SrgbBlock;
    case MTL::PixelFormatASTC_4x4_LDR:
      return TextureFormat::ASTC4x4UnormBlock;
    case MTL::PixelFormatRGBA16Float:
      return TextureFormat::R16G16B16A16Sfloat;
    default:
      ASSERT(0 && "unhandled texture format");
      return TextureFormat::Undefined;
  }
  return TextureFormat::Undefined;
}

MTL::PixelFormat convert(rhi::TextureFormat format) {
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
    case TextureFormat::ASTC4x4SrgbBlock:
      return MTL::PixelFormatASTC_4x4_sRGB;
    case TextureFormat::ASTC4x4UnormBlock:
      return MTL::PixelFormatASTC_4x4_LDR;
    case TextureFormat::R16G16B16A16Sfloat:
      return MTL::PixelFormatRGBA16Float;
    default:
      ASSERT(0 && "unhandled texture format");
      return MTL::PixelFormatInvalid;
  }
  return MTL::PixelFormatInvalid;
}

MTL::ResourceOptions convert(rhi::StorageMode mode) {
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

MTL::TextureUsage convert(rhi::TextureUsage usage) {
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

MTL::Stages convert_stage(rhi::PipelineStage stage) {
  MTL::Stages result{};
  if (stage & (rhi::PipelineStage_AllCommands)) {
    result |= MTL::StageAll;
  }
  if (stage & (rhi::PipelineStage_FragmentShader | rhi::PipelineStage_EarlyFragmentTests |
               rhi::PipelineStage_LateFragmentTests | rhi::PipelineStage_ColorAttachmentOutput |
               rhi::PipelineStage_AllGraphics)) {
    result |= MTL::StageFragment;
  }
  if (stage & (rhi::PipelineStage_MeshShader | rhi::PipelineStage_AllGraphics)) {
    result |= MTL::StageMesh;
  }
  if (stage & (rhi::PipelineStage_TaskShader | rhi::PipelineStage_AllGraphics)) {
    result |= MTL::StageObject;
  }
  if (stage & (rhi::PipelineStage_VertexShader | rhi::PipelineStage_VertexInput |
               rhi::PipelineStage_AllGraphics | rhi::PipelineStage_DrawIndirect)) {
    result |= MTL::StageVertex;
  }
  if (stage & (rhi::PipelineStage_ComputeShader)) {
    result |= MTL::StageDispatch;
  }
  if (stage & (rhi::PipelineStage_AllTransfer)) {
    result |= MTL::StageBlit;
  }
  return result;
}
}  // namespace mtl::util
