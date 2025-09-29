#pragma once

#include "core/Handle.hpp"
#include "core/Pool.hpp"

struct ModelGPUResources;
struct ModelInstanceGPUResources;

using ModelGPUHandle = GenerationalHandle<ModelGPUResources>;
using ModelInstanceGPUHandle = GenerationalHandle<ModelInstanceGPUResources>;

namespace rhi {

class Buffer;
class Device;
class Texture;

using BufferHandle = GenerationalHandle<Buffer>;
using BufferHandleHolder = Holder<BufferHandle, ::rhi::Device>;
using TextureHandle = GenerationalHandle<::rhi::Texture>;
using TextureHandleHolder = Holder<TextureHandle, ::rhi::Device>;

}  // namespace rhi
