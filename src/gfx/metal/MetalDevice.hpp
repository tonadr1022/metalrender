#pragma once

#include <memory>

#include "gfx/Device.hpp"

namespace NS {
class AutoreleasePool;
}
namespace MTL {
class Device;
}

class MetalDevice : public rhi::Device {
 public:
  void init();
  void shutdown() override;
  [[nodiscard]] void* get_native_device() const override { return device_; }

  [[nodiscard]] MTL::Device* get_device() const { return device_; }

 private:
  NS::AutoreleasePool* ar_pool_{};
  MTL::Device* device_{};
};
std::unique_ptr<MetalDevice> create_metal_device();
