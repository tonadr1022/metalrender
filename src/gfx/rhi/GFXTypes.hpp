#pragma once

#include "core/BitUtil.hpp"
#include "core/Config.hpp"
#include "core/Handle.hpp"
#include "core/Math.hpp"  // IWYU pragma: keep
#include "core/Pool.hpp"

namespace TENG_NAMESPACE {

namespace rhi {

class Buffer;
class Device;
class Texture;
class Pipeline;
class Sampler;
class QueryPool;
class Swapchain;

#define MAKE_HANDLE(name)                        \
  using name##Handle = GenerationalHandle<name>; \
  using name##HandleHolder = Holder<name##Handle, Device>

MAKE_HANDLE(Texture);
MAKE_HANDLE(Buffer);
MAKE_HANDLE(Pipeline);
MAKE_HANDLE(Sampler);
MAKE_HANDLE(QueryPool);
MAKE_HANDLE(Swapchain);

#undef MAKE_HANDLE

constexpr uint32_t k_invalid_bindless_idx = UINT32_MAX;

enum class TextureFormat : uint8_t {
  Undefined,
  R8G8B8A8Srgb,
  R8G8B8A8Unorm,
  B8G8R8A8Unorm,
  B8G8R8A8Srgb,
  R16G16B16A16Sfloat,
  R32G32B32A32Sfloat,
  D32float,
  R32float,
  ASTC4x4UnormBlock,
  ASTC4x4SrgbBlock,
};

enum class StorageMode : uint8_t {
  GPUOnly,
  CPUAndGPU,
  // GPU optimal. On apple silicon, this is shared
  Default
};

using TextureUsageFlags = uint32_t;
enum class TextureUsage : TextureUsageFlags {
  None = 0,
  Storage = 1 << 0,
  Sample = 1 << 1,
  ShaderWrite = 1 << 2,
  ColorAttachment = 1 << 3,
  DepthStencilAttachment = 1 << 4,
  TransferSrc = 1 << 5,
  TransferDst = 1 << 6,
};

AUGMENT_ENUM_CLASS(TextureUsage);

using DefaultIndexT = uint32_t;

enum class IndexType : uint8_t {
  Uint16,
  Uint32,
};

enum TextureDescFlags { TextureDescFlags_None, TextureDescFlags_PixelFormatView };

struct TextureDesc {
  TextureFormat format{TextureFormat::Undefined};
  StorageMode storage_mode{StorageMode::GPUOnly};
  TextureUsage usage{TextureUsage::None};
  glm::uvec3 dims{1};
  uint32_t mip_levels{1};
  uint32_t array_length{1};
  bool bindless{true};
  TextureDescFlags flags{};
  const char* name{};
};

enum class BufferUsage : uint8_t {
  None = 0,
  Storage = 1 << 0,
  Indirect = 1 << 1,
  Vertex = 1 << 2,
  Index = 1 << 3,
  Uniform = 1 << 4,
};

AUGMENT_ENUM_CLASS(BufferUsage);

struct BufferDesc {
  StorageMode storage_mode{StorageMode::Default};
  BufferUsage usage{BufferUsage::None};
  size_t size{};
  bool bindless{true};
  bool random_host_access{};
  bool force_gpu_only{};
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

enum class LogicOp : uint8_t {
  Clear = 0,
  And = 1,
  AndReverse = 2,
  Copy = 3,
  AndInverted = 4,
  NoOp = 5,
  Xor = 6,
  Or = 7,
  Nor = 8,
  Equivalent = 9,
  Invert = 10,
  OrReverse = 11,
  CopyInverted = 12,
  OrInverted = 13,
  Nand = 14,
  Set = 15,
};

AUGMENT_ENUM_CLASS(LogicOp);

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

enum class PipelineStage : uint64_t {
  None = 0,
  TopOfPipe = 0x1ull,
  DrawIndirect = 0x2ull,
  VertexInput = 0x4ull,
  IndexInput = 0x1000000000ULL,
  VertexShader = 0x8ull,
  TaskShader = 0x00080000ULL,
  MeshShader = 0x00100000ULL,
  FragmentShader = 0x40ull,
  EarlyFragmentTests = 0x100ull,
  LateFragmentTests = 0x200ull,
  ColorAttachmentOutput = 0x400ull,
  ComputeShader = 0x800ull,
  AllTransfer = 0x1000ull,
  BottomOfPipe = 0x2000ull,
  Host = 0x4000ull,
  AllGraphics = 0x8000ull,
  AllCommands = 0x10000ull,
  Blit = 0x400000000ULL,
};

AUGMENT_ENUM_CLASS(PipelineStage);

enum class AccessFlags : uint64_t {
  None = 0ULL,
  IndirectCommandRead = 0X00000001ULL,
  IndexRead = 0X00000002ULL,
  VertexAttributeRead = 0X00000004ULL,
  UniformRead = 0X00000008ULL,
  InputAttachmentRead = 0X00000010ULL,
  ShaderRead = 0X00000020ULL,
  ShaderWrite = 0X00000040ULL,
  ColorAttachmentRead = 0X00000080ULL,
  ColorAttachmentWrite = 0X00000100ULL,
  DepthStencilRead = 0X00000200ULL,
  DepthStencilWrite = 0X00000400ULL,
  TransferRead = 0X00000800ULL,
  TransferWrite = 0X00001000ULL,
  HostRead = 0X00002000ULL,
  HostWrite = 0X00004000ULL,
  MemoryRead = 0X00008000ULL,
  MemoryWrite = 0X00010000ULL,
  ShaderSampledRead = 0X100000000ULL,
  ShaderStorageRead = 0X200000000ULL,
  ShaderStorageWrite = 0X400000000ULL,
  AnyRead = rhi::AccessFlags::IndirectCommandRead | rhi::AccessFlags::IndexRead |
            rhi::AccessFlags::VertexAttributeRead | rhi::AccessFlags::UniformRead |
            rhi::AccessFlags::InputAttachmentRead | rhi::AccessFlags::ShaderRead |
            rhi::AccessFlags::ColorAttachmentRead | rhi::AccessFlags::DepthStencilRead |
            rhi::AccessFlags::TransferRead | rhi::AccessFlags::HostRead |
            rhi::AccessFlags::MemoryRead | rhi::AccessFlags::ShaderSampledRead |
            rhi::AccessFlags::ShaderStorageRead,
  AnyWrite = rhi::AccessFlags::ShaderWrite | rhi::AccessFlags::ColorAttachmentWrite |
             rhi::AccessFlags::DepthStencilWrite | rhi::AccessFlags::TransferWrite |
             rhi::AccessFlags::HostWrite | rhi::AccessFlags::MemoryWrite |
             rhi::AccessFlags::ShaderStorageWrite,
};

AUGMENT_ENUM_CLASS(AccessFlags);

enum ImageAspect {
  ImageAspect_Color = (1 << 0),
  ImageAspect_Depth = (1 << 1),
  ImageAspect_Stencil = (1 << 2),
};

enum class ResourceState : uint32_t {
  None = 0,
  ColorWrite = 1ULL << 1,  // Stage == ColorAttachmentOutput, Access == ColorWrite
  ColorRead = 1ULL << 2,   // Stage == ColorAttachmentOutput, Access == ColorRead
  ColorRW =
      ColorRead | ColorWrite,    // Stage == ColorAttachmentOutput, Access == ColorRead | ColorWrite
  DepthStencilRead = 1ULL << 3,  // Stage == Early/LateFragmentTests, Access == DepthStencilRead
  DepthStencilWrite = 1ULL << 4,  // Stage == Early/LateFragmentTests, Access == DepthStencilWrite
  DepthStencilRW =
      DepthStencilRead | DepthStencilWrite,  // Stage == Early/LateFragmentTests, Access ==
                                             // DepthStencilRead | DepthStencilWrite
  VertexRead = 1ULL << 5,                    // Stage == VertexInput, Access == VertexRead
  IndexRead = 1ULL << 6,                     // Stage == IndexInput, Access == IndexRead
  IndirectRead = 1ULL << 7,                  // Stage == DrawIndirect, Access == IndirectRead
  ComputeRead = 1ULL << 8,                   // Stage == Compute, Access == ComputeRead
  ComputeWrite = 1ULL << 9,                  // Stage == Compute, Access == ComputeWrite
  ComputeRW = ComputeRead | ComputeWrite,  // Stage == Compute, Access == ComputeRead | ComputeWrite
  TransferRead = 1ULL << 10,               // Stage == Transfer, Access == TransferRead
  TransferWrite = 1ULL << 11,              // Stage == Transfer, Access == TransferWrite
  FragmentStorageRead = 1ULL << 12,        // Stage == Fragment, Access == ShaderRead
  ComputeSample = 1ULL << 13,              // Stage == Compute, Access == ShaderSample
  FragmentSample = 1ULL << 14,             // Stage == Fragment, Access == ShaderSample
  ShaderRead = 1ULL << 15,                 // Stage == AllCommands, Access == ShaderRead
  SwapchainPresent = 1ULL << 16,           // Stage == BottomOfPipe, no access
  AnyRead = ColorRead | DepthStencilRead | VertexRead | IndexRead | IndirectRead | ComputeRead |
            TransferRead | FragmentSample | FragmentStorageRead | ComputeSample | ShaderRead,
  AnyWrite = ColorWrite | DepthStencilWrite | ComputeWrite | TransferWrite,
};

AUGMENT_ENUM_CLASS(ResourceState);

// resource state implying src/dst access/stage inspiration from WickedEngine

enum class ShaderTarget : uint8_t {
  None = 0,
  Spirv = (1 << 0),
  MSL = (1 << 1),
};

AUGMENT_ENUM_CLASS(ShaderTarget);

}  // namespace rhi

}  // namespace TENG_NAMESPACE
