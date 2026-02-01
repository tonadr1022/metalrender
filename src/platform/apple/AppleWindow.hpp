#pragma once

#include "Window.hpp"
#include "core/Config.hpp"

namespace CA {
class MetalLayer;
}

namespace TENG_NAMESPACE {

class AppleWindow final : public Window {
 public:
  ~AppleWindow() override = default;
  void init(InitInfo& init_info) override;

  void set_fullscreen([[maybe_unused]] bool fullscreen) override;

  [[nodiscard]] bool get_fullscreen() const override;
  bool fullscreen_{};
};

void set_layer_for_window(GLFWwindow* window, CA::MetalLayer* layer);

}  // namespace TENG_NAMESPACE
