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

class MetalDevice : public rhi::Device {
 public:
  void init();
  void shutdown() override;
  [[nodiscard]] void* get_native_device() const override { return device_; }

  [[nodiscard]] MTL::Device* get_device() const { return device_; }
  MTL::Texture* create_texture(const rhi::TextureDesc& desc) override;

  rhi::BufferHandle create_buffer(const rhi::BufferDesc& desc) override;
  rhi::BufferHandleHolder create_buffer_h(const rhi::BufferDesc& desc) override {
    return rhi::BufferHandleHolder{create_buffer(desc), this};
  }
  rhi::Buffer* get_buffer(const rhi::BufferHandleHolder& handle) override {
    return buffer_pool_.get(handle.handle);
  }
  rhi::Buffer* get_buffer(rhi::BufferHandle handle) override { return buffer_pool_.get(handle); }

  void destroy(rhi::BufferHandle handle) override;

 private:
  Pool<rhi::BufferHandle, MetalBuffer> buffer_pool_{64};
  NS::AutoreleasePool* ar_pool_{};
  MTL::Device* device_{};
};
std::unique_ptr<MetalDevice> create_metal_device();
