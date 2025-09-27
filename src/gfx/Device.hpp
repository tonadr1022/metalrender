#pragma once

#include "Buffer.hpp"
#include "core/Handle.hpp"
#include "core/Pool.hpp"

namespace rhi {

class Device;

using BufferHandle = GenerationalHandle<Buffer>;
using BufferHandleHolder = Holder<BufferHandle, Device>;

class Device {
 public:
  virtual ~Device() = default;
  [[nodiscard]] virtual void* get_native_device() const = 0;
  virtual void shutdown() = 0;

  virtual MTL::Texture* create_texture(const rhi::TextureDesc& desc) = 0;
  virtual BufferHandle create_buffer(const rhi::BufferDesc& desc) = 0;
  virtual BufferHandleHolder create_buffer_h(const rhi::BufferDesc& desc) = 0;
  virtual Buffer* get_buffer(const BufferHandleHolder& handle) = 0;
  virtual Buffer* get_buffer(BufferHandle handle) = 0;
  virtual void destroy(BufferHandle handle) = 0;
};

}  // namespace rhi
