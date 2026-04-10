#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class Camera {
 public:
  [[nodiscard]] glm::mat4 get_view_mat() const;
  void calc_vectors();

  glm::vec3 pos{};
  float pitch{}, yaw{};
  glm::vec3 front{}, right{};
  float max_velocity{5.f};
  float move_speed{10.0f};
  float mouse_sensitivity{.1};
};
