#pragma once

#include <memory>

#include "gfx/Device.hpp"

namespace MTL {
class Device;
}

class MetalDevice : public rhi::Device {
 public:
  MetalDevice();
  void shutdown() override;
  [[nodiscard]] void* get_native_device() const override { return device_; }

 private:
  MTL::Device* device_{};
};
std::unique_ptr<MetalDevice> create_metal_device();
