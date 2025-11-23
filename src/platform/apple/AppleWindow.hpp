#pragma once

#include "Window.hpp"

class AppleWindow final : public Window {
 public:
  ~AppleWindow() override = default;
  void init(KeyCallbackFn key_callback_fn, CursorPosCallbackFn cursor_pos_callback_fn,
            bool transparent_window) override;

  void set_fullscreen([[maybe_unused]] bool fullscreen) override;

  [[nodiscard]] bool get_fullscreen() const override;
  bool fullscreen_{};
};
