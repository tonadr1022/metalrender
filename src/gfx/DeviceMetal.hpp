#pragma once

#include <memory>

#include "gfx/Device.hpp"

namespace MTL {
class Device;
}

class DeviceMetal : public RHIDevice {
 public:
  void init() override;
  void shutdown() override;
  [[nodiscard]] void* get_native_device() const override { return device_; }

 private:
  MTL::Device* device_{};
};
std::unique_ptr<RHIDevice> create_metal_device();
