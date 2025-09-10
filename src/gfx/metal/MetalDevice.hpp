#pragma once

#include <memory>

#include "gfx/Device.hpp"

namespace MTL {
class Device;
class CommandQueue;
}  // namespace MTL

class MetalDevice : public rhi::Device {
 public:
  MetalDevice();
  void shutdown() override;
  [[nodiscard]] void* get_native_device() const override { return mtl_device_; }
  rhi::Texture create_texture(const rhi::TextureDesc& desc) override;
  void generate_mipmaps(std::span<rhi::Texture> textures) override;
  void generate_mipmaps(const rhi::Texture& textures) override;

 private:
  MTL::Device* mtl_device_{};
  MTL::CommandQueue* main_queue_{};
};
std::unique_ptr<MetalDevice> create_metal_device();
