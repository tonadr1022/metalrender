#pragma once

#include <functional>
#include <glm/vec2.hpp>

namespace rhi {
class Device;
}

class Window {
 public:
  using KeyCallbackFn = std::function<void(int key, int action, int mods)>;
  using CursorPosCallbackFn = std::function<void(double x_pos, double y_pos)>;

  virtual void init(rhi::Device* device, KeyCallbackFn key_callback_fn,
                    CursorPosCallbackFn cursor_pos_callback_fn) = 0;
  virtual void shutdown() = 0;
  virtual ~Window() = default;

  [[nodiscard]] virtual bool should_close() const = 0;
  virtual void poll_events() = 0;
  virtual glm::uvec2 get_window_size() = 0;

 private:
};
