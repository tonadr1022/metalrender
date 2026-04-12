#include "FpsCameraController.hpp"

#include <GLFW/glfw3.h>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

void FpsCameraController::update(GLFWwindow* window, float dt, bool imgui_blocks_keyboard) {
  if (imgui_blocks_keyboard) {
    return;
  }
  update_keyboard(window, dt);
}

void FpsCameraController::update_keyboard(GLFWwindow* window, float dt) {
  cam_.calc_vectors();
  auto get_key = [&](int key) { return glfwGetKey(window, key) == GLFW_PRESS; };
  glm::vec3 acceleration{};
  bool accelerating{};

  if (get_key(GLFW_KEY_W) || get_key(GLFW_KEY_I)) {
    acceleration += cam_.front;
    accelerating = true;
  }
  if (get_key(GLFW_KEY_S) || get_key(GLFW_KEY_K)) {
    acceleration -= cam_.front;
    accelerating = true;
  }
  if (get_key(GLFW_KEY_A) || get_key(GLFW_KEY_J)) {
    acceleration -= cam_.right;
    accelerating = true;
  }
  if (get_key(GLFW_KEY_D) || get_key(GLFW_KEY_L)) {
    acceleration += cam_.right;
    accelerating = true;
  }

  if (get_key(GLFW_KEY_Y) || get_key(GLFW_KEY_R)) {
    acceleration += glm::vec3(0, 1, 0);
    accelerating = true;
  }
  if (get_key(GLFW_KEY_H) || get_key(GLFW_KEY_F)) {
    acceleration += glm::vec3(0, -1, 0);
    accelerating = true;
  }

  if (get_key(GLFW_KEY_B)) {
    cam_.move_speed *= 1.1f;
    cam_.max_velocity *= 1.1f;
  }
  if (get_key(GLFW_KEY_V)) {
    cam_.move_speed /= 1.1f;
    cam_.max_velocity /= 1.1f;
  }

  if (accelerating) {
    if (glm::length(acceleration) > 0.0001f) {
      acceleration = glm::normalize(acceleration) * cam_.move_speed;
    } else {
      acceleration = glm::vec3(0.0f);
    }
  }

  cam_.pos += acceleration * cam_.max_velocity * dt;
}

void FpsCameraController::apply_mouse_delta(glm::vec2 offset) {
  offset *= cam_.mouse_sensitivity;
  cam_.yaw += offset.x;
  cam_.pitch += look_pitch_sign_ * offset.y;
  cam_.pitch = glm::clamp(cam_.pitch, -89.f, 89.f);
  cam_.calc_vectors();
}

void FpsCameraController::on_cursor_pos(double x, double y) {
  const glm::vec2 pos = {static_cast<float>(x), static_cast<float>(y)};
  if (first_mouse_) {
    first_mouse_ = false;
    last_pos_ = pos;
    return;
  }

  const glm::vec2 offset = {pos.x - last_pos_.x, last_pos_.y - pos.y};
  last_pos_ = pos;
  if (mouse_captured_) {
    apply_mouse_delta(offset);
  }
}

void FpsCameraController::set_mouse_captured(GLFWwindow* window, bool on) {
  mouse_captured_ = on;
  if (on) {
    first_mouse_ = true;
  }
  glfwSetInputMode(window, GLFW_CURSOR, on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void FpsCameraController::toggle_mouse_capture(GLFWwindow* window) {
  set_mouse_captured(window, !mouse_captured_);
}
