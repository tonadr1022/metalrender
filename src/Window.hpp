#pragma once

#include <glm/vec2.hpp>

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
  virtual glm::uvec2 get_window_size() = 0;

 private:
};
