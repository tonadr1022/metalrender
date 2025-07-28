#pragma once

namespace MTL {
class Device;
}

class DeviceMetal {
 public:
  void init();
  void shutdown();
  [[nodiscard]] void* get_native_device() const { return device_; }

 private:
  MTL::Device* device_{};
};
