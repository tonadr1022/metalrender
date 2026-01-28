#pragma once

#include "core/Handle.hpp"
#include "core/Math.hpp"  // IWYU pragma: keep
#include "core/Pool.hpp"

struct ModelGPUResources;
struct ModelInstanceGPUResources;

using ModelGPUHandle = GenerationalHandle<ModelGPUResources>;
using ModelInstanceGPUHandle = GenerationalHandle<ModelInstanceGPUResources>;

namespace rhi {

class Buffer;
class Device;
class Texture;
class Pipeline;
class Sampler;

using BufferHandle = GenerationalHandle<Buffer>;
using BufferHandleHolder = Holder<BufferHandle, ::rhi::Device>;
using TextureHandle = GenerationalHandle<::rhi::Texture>;
using TextureHandleHolder = Holder<TextureHandle, ::rhi::Device>;
using PipelineHandle = GenerationalHandle<Pipeline>;
using PipelineHandleHolder = Holder<PipelineHandle, ::rhi::Device>;
using SamplerHandle = GenerationalHandle<Sampler>;
using SamplerHandleHolder = Holder<SamplerHandle, ::rhi::Device>;

}  // namespace rhi

using UntypedDeleterFuncPtr = void (*)(void*);

struct ModelInstance;
using ModelHandle = GenerationalHandle<ModelInstance>;
