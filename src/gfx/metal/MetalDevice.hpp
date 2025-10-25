#pragma once

#include <Metal/MTLArgumentEncoder.hpp>
#include <Metal/MTLBuffer.hpp>
#include <memory>

#include "MetalBuffer.hpp"
#include "MetalTexture.hpp"
#include "core/Allocator.hpp"
#include "core/Handle.hpp"
#include "core/Pool.hpp"
#include "gfx/Device.hpp"
#include "shader_constants.h"

namespace rhi {

struct TextureDesc;

}  // namespace rhi

namespace NS {
class AutoreleasePool;
}
namespace MTL {
class Device;
class Texture;
class RenderCommandEncoder;

}  // namespace MTL

class MetalDevice;

class MetalDevice : public rhi::Device {
 public:
  void init();
  void shutdown() override;
  [[nodiscard]] void* get_native_device() const override { return device_; }

  [[nodiscard]] MTL::Device* get_device() const { return device_; }

  rhi::BufferHandle create_buf(const rhi::BufferDesc& desc) override;
  rhi::BufferHandleHolder create_buf_h(const rhi::BufferDesc& desc) override {
    return rhi::BufferHandleHolder{create_buf(desc), this};
  }
  rhi::Buffer* get_buf(const rhi::BufferHandleHolder& handle) override {
    return buffer_pool_.get(handle.handle);
  }
  rhi::Buffer* get_buf(rhi::BufferHandle handle) override { return buffer_pool_.get(handle); }

  rhi::TextureHandle create_tex(const rhi::TextureDesc& desc) override;
  rhi::TextureHandleHolder create_tex_h(const rhi::TextureDesc& desc) override {
    return rhi::TextureHandleHolder{create_tex(desc), this};
  }
  rhi::Texture* get_tex(rhi::TextureHandle handle) override { return texture_pool_.get(handle); }

  rhi::Texture* get_tex(const rhi::TextureHandleHolder& handle) override {
    return get_tex(handle.handle);
  }

  void destroy(rhi::BufferHandle handle) override;
  void destroy(rhi::TextureHandle handle) override;

  // MTL::Buffer* get_bindless_arg_buffer() {
  //   return reinterpret_cast<MetalBuffer*>(get_buf(bindless_arg_buffer_))->buffer();
  // }

  void use_bindless_buffer(MTL::RenderCommandEncoder* enc);

 private:
  BlockPool<rhi::BufferHandle, MetalBuffer> buffer_pool_{128, 1, true};
  BlockPool<rhi::TextureHandle, MetalTexture> texture_pool_{128, 1, true};
  IndexAllocator texture_index_allocator_{k_max_textures};
  NS::AutoreleasePool* ar_pool_{};
  MTL::Device* device_{};

  MTL::Buffer* get_mtl_buf(const rhi::BufferHandleHolder& handle) {
    return reinterpret_cast<MetalBuffer*>(get_buf(handle))->buffer();
  }
};
std::unique_ptr<MetalDevice> create_metal_device();
