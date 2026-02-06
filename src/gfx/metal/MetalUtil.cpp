#include "MetalUtil.hpp"

#include <Metal/Metal.hpp>
#include <cassert>

#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace TENG_NAMESPACE {

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
    case MTL::PixelFormatRGBA32Float:
      return TextureFormat::R32G32B32A32Sfloat;
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
    case TextureFormat::R32G32B32A32Sfloat:
      return MTL::PixelFormatRGBA32Float;
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

  if (has_flag(usage, TextureUsage::Sample | TextureUsage::Storage)) {
    result |= MTL::TextureUsageShaderRead;
  }
  if (has_flag(usage, rhi::TextureUsage::ColorAttachment) ||
      has_flag(usage, rhi::TextureUsage::DepthStencilAttachment)) {
    result |= MTL::TextureUsageRenderTarget;
  }
  if (has_flag(usage, TextureUsage::ShaderWrite)) {
    result |= MTL::TextureUsageShaderWrite;
  }

  return result;
}

MTL::Stages convert_stage(rhi::PipelineStage stage) {
  using enum rhi::PipelineStage;
  MTL::Stages result{};
  if (has_flag(stage, AllCommands)) {
    result |= MTL::StageAll;
  }
  if (has_flag(stage, FragmentShader) || has_flag(stage, EarlyFragmentTests) ||
      has_flag(stage, LateFragmentTests) || has_flag(stage, ColorAttachmentOutput) ||
      has_flag(stage, AllGraphics)) {
    result |= MTL::StageFragment;
  }
  if (has_flag(stage, MeshShader) || has_flag(stage, AllGraphics)) {
    result |= MTL::StageMesh;
  }
  if (has_flag(stage, TaskShader) || has_flag(stage, AllGraphics)) {
    result |= MTL::StageObject;
  }
  if (has_flag(stage, VertexShader) || has_flag(stage, VertexInput) ||
      has_flag(stage, IndexInput) || has_flag(stage, AllGraphics) ||
      has_flag(stage, DrawIndirect)) {
    result |= MTL::StageVertex;
  }
  if (has_flag(stage, ComputeShader)) {
    result |= MTL::StageDispatch;
  }
  if (has_flag(stage, AllTransfer)) {
    result |= MTL::StageBlit;
  }
  return result;
}

MTL::Stages convert_stages(rhi::ResourceState state) {
  using enum rhi::ResourceState;
  MTL::Stages result{};
  if (has_flag(state, ColorRead) || has_flag(state, ColorWrite)) {
    result |= MTL::StageFragment;
  }
  if (has_flag(state, DepthStencilRead) || has_flag(state, DepthStencilWrite)) {
    result |= MTL::StageFragment;
  }
  if (has_flag(state, ComputeRead) || has_flag(state, ComputeWrite)) {
    result |= MTL::StageDispatch;
  }
  if (has_flag(state, VertexRead) || has_flag(state, IndexRead) || has_flag(state, IndirectRead)) {
    result |= MTL::StageVertex;
  }
  if (has_flag(state, TransferRead) || has_flag(state, TransferWrite)) {
    result |= MTL::StageBlit;
  }
  if (has_flag(state, FragmentStorageRead) || has_flag(state, FragmentSample)) {
    result |= MTL::StageFragment;
  }
  if (has_flag(state, ComputeSample)) {
    result |= MTL::StageDispatch;
  }
  if (has_flag(state, ShaderRead)) {
    result |= MTL::StageAll;
  }

  return result;
}
}  // namespace mtl::util

}  // namespace TENG_NAMESPACE
