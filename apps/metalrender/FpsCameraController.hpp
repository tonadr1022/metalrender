#pragma once

#include <glm/vec2.hpp>

#include "Camera.hpp"

struct GLFWwindow;

class FpsCameraController {
 public:
  Camera& camera() { return cam_; }
  const Camera& camera() const { return cam_; }

  [[nodiscard]] bool mouse_captured() const { return mouse_captured_; }

  void update(GLFWwindow* window, float dt, bool imgui_blocks_keyboard);
  void on_cursor_pos(double x, double y);
  void set_mouse_captured(GLFWwindow* window, bool on);
  void toggle_mouse_capture(GLFWwindow* window);

  /// Multiplier on vertical mouse delta before applying pitch (default 1). Use -1 when the
  /// render path flips the framebuffer vertically (e.g. Vulkan negative viewport height).
  void set_look_pitch_sign(float sign) { look_pitch_sign_ = sign; }

 private:
  void update_keyboard(GLFWwindow* window, float dt);
  void apply_mouse_delta(glm::vec2 offset);

  Camera cam_{};
  bool first_mouse_{true};
  glm::vec2 last_pos_{};
  bool mouse_captured_{false};
  float look_pitch_sign_{1.f};
};
