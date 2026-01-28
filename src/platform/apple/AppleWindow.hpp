#pragma once

#include "Window.hpp"

class AppleWindow final : public Window {
 public:
  ~AppleWindow() override = default;
  void init(InitInfo& init_info) override;

  void set_fullscreen([[maybe_unused]] bool fullscreen) override;

  [[nodiscard]] bool get_fullscreen() const override;
  bool fullscreen_{};
};

namespace CA {
class MetalLayer;
}

void set_layer_for_window(GLFWwindow* window, CA::MetalLayer* layer);
