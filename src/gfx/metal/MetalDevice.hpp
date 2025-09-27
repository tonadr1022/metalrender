#pragma once

#include <memory>

#include "Metal/MTLBuffer.hpp"
#include "MetalBuffer.hpp"
#include "core/Handle.hpp"
#include "core/Pool.hpp"
#include "gfx/Device.hpp"

namespace rhi {

struct TextureDesc;

}  // namespace rhi

namespace NS {
class AutoreleasePool;
}
namespace MTL {
class Device;
class Texture;
}  // namespace MTL

class MetalDevice;

using BufferHandle = GenerationalHandle<MetalBuffer>;
using BufferHandleHolder = Holder<BufferHandle, MetalDevice>;

class MetalDevice : public rhi::Device {
 public:
  void init();
  void shutdown() override;
  [[nodiscard]] void* get_native_device() const override { return device_; }

  [[nodiscard]] MTL::Device* get_device() const { return device_; }
  MTL::Texture* create_texture(const rhi::TextureDesc& desc);
  BufferHandle create_buffer(const rhi::BufferDesc& desc);

  BufferHandleHolder create_bufferh(const rhi::BufferDesc& desc) {
    return BufferHandleHolder{create_buffer(desc), this};
  }

  void destroy(BufferHandle handle);

 private:
  Pool<BufferHandle, MetalBuffer> buffer_pool_{64};
  NS::AutoreleasePool* ar_pool_{};
  MTL::Device* device_{};
};
std::unique_ptr<MetalDevice> create_metal_device();
