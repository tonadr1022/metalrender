#include "Camera.hpp"

#include <GLFW/glfw3.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>
#include <glm/trigonometric.hpp>

void Camera::calc_vectors() {
  glm::vec3 dir;
  dir.x = glm::cos(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
  dir.y = glm::sin(glm::radians(pitch));
  dir.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
  front = glm::normalize(dir);
  right = glm::normalize(glm::cross(front, {0, 1, 0}));
}

glm::mat4 Camera::get_view_mat() const { return glm::lookAt(pos, pos + front, glm::vec3{0, 1, 0}); }

bool Camera::update_pos(GLFWwindow* window, float dt) {
  calc_vectors();
  auto get_key = [&](int key) { return glfwGetKey(window, key) == GLFW_PRESS; };
  glm::vec3 acceleration{};
  bool accelerating{};

  if (get_key(GLFW_KEY_W) || get_key(GLFW_KEY_I)) {
    acceleration += front;
    accelerating = true;
  }
  if (get_key(GLFW_KEY_S) || get_key(GLFW_KEY_K)) {
    acceleration -= front;
    accelerating = true;
  }
  if (get_key(GLFW_KEY_A) || get_key(GLFW_KEY_J)) {
    acceleration -= right;
    accelerating = true;
  }
  if (get_key(GLFW_KEY_D) || get_key(GLFW_KEY_L)) {
    acceleration += right;
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
    acceleration_strength *= 1.1f;
    max_velocity *= 1.1f;
  }
  if (get_key(GLFW_KEY_V)) {
    acceleration_strength /= 1.1f;
    max_velocity /= 1.1f;
  }

  if (accelerating) {
    if (glm::length(acceleration) > 0.0001f) {
      acceleration = glm::normalize(acceleration) * acceleration_strength;
    } else {
      acceleration = glm::vec3(0.0f);
    }
  }

  velocity += acceleration * dt;
  velocity *= damping;

  velocity = glm::clamp(velocity, -max_velocity, max_velocity);

  pos += velocity * dt;

  return accelerating || !glm::all(glm::equal(velocity, glm::vec3{0}, glm::epsilon<float>()));
}

bool Camera::process_mouse(glm::vec2 offset) {
  offset *= mouse_sensitivity;
  yaw += offset.x;
  pitch += offset.y;
  pitch = glm::clamp(pitch, -89.f, 89.f);
  calc_vectors();
  return !glm::all(glm::equal(offset, glm::vec2{0}, glm::epsilon<float>()));
}
