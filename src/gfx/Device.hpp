#pragma once

namespace rhi {

class Device {
 public:
  virtual ~Device() = default;
  [[nodiscard]] virtual void* get_native_device() const = 0;
  virtual void shutdown() = 0;
};

}  // namespace rhi
