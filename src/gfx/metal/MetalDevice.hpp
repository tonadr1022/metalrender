#pragma once

#include <memory>

#include "gfx/Device.hpp"

struct TextureDesc;

namespace NS {
class AutoreleasePool;
}
namespace MTL {
class Device;
class Texture;
}  // namespace MTL

class MetalDevice : public rhi::Device {
 public:
  void init();
  void shutdown() override;
  [[nodiscard]] void* get_native_device() const override { return device_; }

  [[nodiscard]] MTL::Device* get_device() const { return device_; }
  MTL::Texture* create_texture(const TextureDesc& desc);

 private:
  NS::AutoreleasePool* ar_pool_{};
  MTL::Device* device_{};
};
std::unique_ptr<MetalDevice> create_metal_device();
