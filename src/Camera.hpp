#pragma once

#include "core/Math.hpp"

struct GLFWwindow;

class Camera {
 public:
  [[nodiscard]] glm::mat4 get_view_mat() const;
  void calc_vectors();
  bool update_pos(GLFWwindow *window, float dt);
  bool process_mouse(glm::vec2 offset);

  glm::vec3 pos{};
  float pitch{}, yaw{};
  glm::vec3 front{}, right{};
  float max_velocity{5.f};
  glm::vec3 velocity{};
  float acceleration_strength{100.0f};
  float damping{0.9f};
  float mouse_sensitivity{.1};
};
