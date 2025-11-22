#pragma once

#include "gfx/RendererTypes.hpp"

namespace rhi {

constexpr uint32_t k_invalid_bindless_idx = UINT32_MAX;

enum class TextureFormat : uint8_t {
  Undefined,
  R8G8B8A8Srgb,
  R8G8B8A8Unorm,
  B8G8R8A8Unorm,
  D32float,
  R32float,
};

enum class StorageMode : uint8_t {
  GPUOnly,
  CPUAndGPU,
  // GPU optimal. On apple silicon, this is shared
  Default
};

enum TextureUsage : uint8_t {
  TextureUsageNone,
  TextureUsageStorage,
  TextureUsageSample,
  TextureUsageShaderWrite,
  TextureUsageColorAttachment,
  TextureUsageDepthStencilAttachment,
};

using DefaultIndexT = uint32_t;

enum class IndexType : uint8_t {
  Uint16,
  Uint32,
};

enum TextureDescFlags { TextureDescFlags_None, TextureDescFlags_PixelFormatView };

struct TextureDesc {
  TextureFormat format{TextureFormat::Undefined};
  StorageMode storage_mode{StorageMode::GPUOnly};
  TextureUsage usage{TextureUsageNone};
  glm::uvec3 dims{1};
  uint32_t mip_levels{1};
  uint32_t array_length{1};
  bool bindless{};
  TextureDescFlags flags{};
  const char* name{};
};

enum BufferUsage : uint8_t {
  BufferUsage_None = 0,
  BufferUsage_Storage = 1 << 0,
  BufferUsage_Indirect = 1 << 1,
  BufferUsage_Vertex = 1 << 2,
  BufferUsage_Index = 1 << 3,
  BufferUsage_Uniform = 1 << 4,
  BufferUsage_Transfer = 1 << 5,
};

struct BufferDesc {
  StorageMode storage_mode{StorageMode::Default};
  BufferUsage usage{BufferUsage_None};
  size_t size{};
  bool bindless{false};
  bool random_host_access{};
  const char* name{};
};

enum class LoadOp : uint8_t { Load, Clear, DontCare };
enum class StoreOp : uint8_t { Store, DontCare };

union ClearValue {
  glm::vec4 color;
  struct {
    float depth;
    uint32_t stencil;
  } depth_stencil;
};

struct RenderingAttachmentInfo {
  enum class Type : uint8_t { Color, DepthStencil };

  TextureHandle image;
  int subresource{-1};
  Type type{Type::Color};
  LoadOp load_op{LoadOp::Load};
  StoreOp store_op{StoreOp::Store};
  ClearValue clear_value{};

  static RenderingAttachmentInfo color_att(TextureHandle image, LoadOp load_op = LoadOp::Load,
                                           ClearValue clear_value = {},
                                           StoreOp store_op = StoreOp::Store,
                                           int subresource = -1) {
    return {.image = image,
            .subresource = subresource,
            .type = Type::Color,
            .load_op = load_op,
            .store_op = store_op,
            .clear_value = clear_value};
  }

  static RenderingAttachmentInfo depth_stencil_att(TextureHandle image,
                                                   LoadOp load_op = LoadOp::Load,
                                                   ClearValue clear_value = {},
                                                   StoreOp store_op = StoreOp::Store,
                                                   int subresource = -1) {
    return {.image = image,
            .subresource = subresource,
            .type = Type::DepthStencil,
            .load_op = load_op,
            .store_op = store_op,
            .clear_value = clear_value};
  }
};

enum class BlendFactor : uint8_t {
  Zero = 0,
  One = 1,
  SrcColor = 2,
  OneMinusSrcColor = 3,
  DstColor = 4,
  OneMinusDstColor = 5,
  SrcAlpha = 6,
  OneMinusSrcAlpha = 7,
  DstAlpha = 8,
  OneMinusDstAlpha = 9,
  ConstantColor = 10,
  OneMinusConstantColor = 11,
  ConstantAlpha = 12,
  OneMinusConstantAlpha = 13,
  SrcAlphaSaturate = 14,
  Src1Color = 15,
  OneMinusSrc1Color = 16,
  Src1Alpha = 17,
  OneMinusSrc1Alpha = 18,
};

enum class BlendOp : uint32_t {
  Add = 0,
  Subtract = 1,
  ReverseSubtract = 2,
  Min = 3,
  Max = 4,
};

enum ColorComponentFlagBits : uint8_t {
  ColorComponentRBit = 0x00000001,
  ColorComponentGBit = 0x00000002,
  ColorComponentBBit = 0x00000004,
  ColorComponentABit = 0x00000008,
};

using ColorComponentFlags = uint32_t;

enum LogicOp : uint8_t {
  LogicOpClear = 0,
  LogicOpAnd = 1,
  LogicOpAndReverse = 2,
  LogicOpCopy = 3,
  LogicOpAndInverted = 4,
  LogicOpNoOp = 5,
  LogicOpXor = 6,
  LogicOpOr = 7,
  LogicOpNor = 8,
  LogicOpEquivalent = 9,
  LogicOpInvert = 10,
  LogicOpOrReverse = 11,
  LogicOpCopyInverted = 12,
  LogicOpOrInverted = 13,
  LogicOpNand = 14,
  LogicOpSet = 15,
};

enum SampleCountFlagBits : uint8_t {
  SampleCount1Bit = 0x00000001,
  SampleCount2Bit = 0x00000002,
  SampleCount4Bit = 0x00000004,
  SampleCount8Bit = 0x00000008,
  SampleCount16Bit = 0x00000010,
  SampleCount32Bit = 0x00000020,
  SampleCount64Bit = 0x00000040,
};
using SampleCountFlags = uint32_t;

enum class StencilOp : uint8_t {
  Keep = 0,
  Zero,
  Replace,
  IncrementAndClamp,
  DecrementAndClamp,
  IncrementAndWrap,
  DecrementAndWrap,
};

enum class CompareOp : uint8_t {
  Never = 0,
  Less,
  Equal,
  LessOrEqual,
  Greater,
  NotEqual,
  GreaterOrEqual,
  Always
};

enum class PrimitiveTopology : uint8_t {
  PointList,
  LineList,
  LineStrip,
  TriangleList,
  TriangleStrip,
  TriangleFan,
  PatchList
};

enum class FilterMode : uint8_t { Nearest, Linear };

enum class AddressMode : uint8_t {
  Repeat,
  MirroredRepeat,
  ClampToEdge,
  ClampToBorder,
  MirrorClampToEdge
};

enum class BorderColor : uint8_t {
  FloatTransparentBlack,
  IntTransparentBlack,
  FloatOpaqueBlack,
  IntOpaqueBlack,
  FloatOpaqueWhite,
  IntOpaqueWhite
};

enum class CullMode : uint8_t {
  None,
  Back,
  Front,
};

enum class PolygonMode : uint8_t {
  Fill,
  Line,
  Point,
};

enum class WindOrder : uint8_t {
  Clockwise,
  CounterClockwise,
};

}  // namespace rhi
