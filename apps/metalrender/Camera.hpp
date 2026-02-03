#pragma once

#include <glm/mat4x4.hpp>

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
  float move_speed{10.0f};
  float mouse_sensitivity{.1};
};
