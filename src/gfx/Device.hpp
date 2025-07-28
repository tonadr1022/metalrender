#pragma once

class RHIDevice {
 public:
  virtual ~RHIDevice() = default;
  [[nodiscard]] virtual void* get_native_device() const = 0;
  virtual void init() = 0;
  virtual void shutdown() = 0;
};
