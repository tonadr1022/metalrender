#include "App.hpp"

#include <glm/gtc/quaternion.hpp>

#include "WindowApple.hpp"
#include "core/Logger.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "glm/ext/matrix_transform.hpp"

namespace {

std::filesystem::path get_resource_dir() {
  std::filesystem::path curr_path = std::filesystem::current_path();
  while (curr_path.has_parent_path()) {
    if (std::filesystem::exists(curr_path / "resources")) {
      return curr_path / "resources";
    }
    curr_path = curr_path.parent_path();
  }
  return "";
}

}  // namespace

glm::mat4 Camera::get_view_mat() const { return glm::lookAt(pos, pos + front, glm::vec3{0, 1, 0}); }

void Camera::calc_vectors() {
  glm::vec3 dir;
  dir.x = glm::cos(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
  dir.y = glm::sin(glm::radians(pitch));
  dir.z = glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch));
  front = glm::normalize(dir);
  right = glm::normalize(glm::cross(front, {0, 1, 0}));
}

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

App::App() {
  resource_dir_ = get_resource_dir();
  shader_dir_ = resource_dir_ / "shaders";
  device_ = create_metal_device();
  window_ = create_apple_window();
  device_->init();
  window_->init(device_.get());
  ResourceManager::init();
  glfwSetWindowUserPointer(window_->get_handle(), this);
  glfwSetCursorPosCallback(window_->get_handle(), [](GLFWwindow* window, double xpos, double ypos) {
    ((App*)glfwGetWindowUserPointer(window))->on_key_event(xpos, ypos);
  });
  renderer_.init(RendererMetal::CreateInfo{
      .device = device_.get(), .window = window_.get(), .resource_dir = resource_dir_});
}

void App::run() {
  renderer_.load_model("/Users/tony/models/glTF-Sample-Assets/Models/Sponza/glTF/Sponza.gltf");
  // renderer_.load_model(resource_dir_ / "models/Cube/glTF/Cube.gltf");

  double last_time = glfwGetTime();
  while (!window_->should_close()) {
    window_->poll_events();
    double curr_time = glfwGetTime();
    auto dt = static_cast<float>(curr_time - last_time);
    camera_.update_pos(window_->get_handle(), dt);

    // glm::vec3 movement{};
    // if (glfwGetKey(window_->get_handle(), GLFW_KEY_I)) {
    //   movement.x++;
    // }
    // if (glfwGetKey(window_->get_handle(), GLFW_KEY_K)) {
    //   movement.x--;
    // }
    // if (glfwGetKey(window_->get_handle(), GLFW_KEY_L)) {
    //   movement.x--;
    // }
    // static float t = 0;
    // t++;
    // float ch = t / 500.f;
    // glm::mat4 view_mat = glm::lookAt(glm::vec3{glm::sin(ch) * 2, 2, glm::cos(ch) * 2},
    // glm::vec3{0},
    //                                  glm::vec3{0, 1, 0});
    // RenderArgs args{view_mat};
    RenderArgs args{.view_mat = camera_.get_view_mat()};
    renderer_.render(args);
  }

  ResourceManager::shutdown();
  window_->shutdown();
  device_->shutdown();
}

void App::on_key_event(double xpos, double ypos) {
  glm::vec2 pos = {xpos, ypos};
  if (first_mouse_) {
    first_mouse_ = false;
    last_pos_ = pos;
    return;
  }
  glm::vec2 offset = {pos.x - last_pos_.x, last_pos_.y - pos.y};
  last_pos_ = pos;
 ` camera_.process_mouse(offset);
}
