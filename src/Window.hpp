#pragma once

namespace rhi {
class Device;
}

class Window {
 public:
  virtual ~Window() = default;
  [[nodiscard]] virtual bool should_close() const = 0;
  virtual void poll_events() = 0;
  virtual void init(rhi::Device* device) = 0;
  virtual void shutdown() = 0;

 private:
};
