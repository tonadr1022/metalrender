#include "App.hpp"

#include <fstream>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/random.hpp>
#include <random>

#include "WindowApple.hpp"
#include "core/Logger.hpp"
#include "core/Util.hpp"
#include "gfx/ResourceManager.hpp"
#include "gfx/metal/MetalDevice.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "imgui.h"
#include "tracy/Tracy.hpp"
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
  ZoneScoped;
  resource_dir_ = get_resource_dir();
  shader_dir_ = resource_dir_ / "shaders";
  load_config();
  device_ = create_metal_device();
  window_ = create_apple_window();
  device_->init();
  window_->init(
      device_.get(), [this](int key, int action, int mods) { on_key_event(key, action, mods); },
      [this](double x_pos, double y_pos) { on_curse_pos_event(x_pos, y_pos); });

  on_hide_mouse_change();

  ResourceManager::init(ResourceManager::CreateInfo{.renderer = &renderer_});
  renderer_.init(RendererMetal::CreateInfo{.device = device_.get(),
                                           .window = window_.get(),
                                           .resource_dir = resource_dir_,
                                           .render_imgui_callback = [this]() { on_imgui(); }});
}
namespace rando {

namespace {

std::random_device rd{};
std::default_random_engine eng{rd()};

void seed(int seed) { eng.seed(seed); }

float get_float(float min, float max) {
  std::uniform_real_distribution<float> dist{min, max};
  return dist(eng);
}

}  // namespace

}  // namespace rando

void App::run() {
  ZoneScoped;
  rando::seed(10000000);
  int scene = 1;
  if (scene == 0) {
    // glm::ivec3 iter{};
    // glm::ivec3 dims{1, 1, 1};
    // float dist = 40.0;
    // for (iter.z = -dims.z; iter.z <= dims.z; iter.z++) {
    //   for (iter.x = -dims.x; iter.x <= dims.x; iter.x++) {
    //     glm::vec3 pos = glm::vec3{iter} * dist;
    // load_model(config_.initial_model_path, glm::translate(glm::mat4{1}, pos));
    //   }
    // }
    load_model(config_.initial_model_path);
  } else if (scene == 1) {
    /*
    size_t count = 100;
    float scale = 300;
      */
    size_t count = 1000;
    float scale = 10;

    for (size_t i = 0; i < count; i++) {
      auto rand_f = []() { return rando::get_float(-100, 100); };
      auto pos = glm::vec3{rand_f(), rand_f(), rand_f()};
      glm::vec3 randomAxis = glm::linearRand(glm::vec3(-1.0f), glm::vec3(1.0f));
      float randomAngle = glm::linearRand(0.0f, glm::two_pi<float>());
      auto rot = glm::angleAxis(randomAngle, glm::normalize(randomAxis));
      load_model(config_.initial_model_path, glm::translate(glm::mat4{1}, pos) *
                                                 glm::mat4_cast(rot) *
                                                 glm::scale(glm::mat4{1}, glm::vec3{scale}));
    }
  } else if (scene == 2) {
    glm::vec3 positions[] = {glm::vec3{0, 0, 0}, glm::vec3{-10, 0, 0}};
    float scales[] = {3, 1};
    for (size_t i = 0; i < ARRAY_SIZE(scales); i++) {
      auto p = positions[i];
      load_model(config_.initial_model_path,
                 glm::translate(glm::mat4{1}, p) * glm::scale(glm::mat4{1}, glm::vec3{scales[i]}));
    }
  }

  double last_time = glfwGetTime();
  while (!window_->should_close()) {
    ZoneScopedN("main loop");
    window_->poll_events();
    const double curr_time = glfwGetTime();
    auto dt = static_cast<float>(curr_time - last_time);
    last_time = curr_time;
    camera_.update_pos(window_->get_handle(), dt);

    for (const auto model : models_) {
      ResourceManager::get().get_model(model)->update_transforms();
    }
    const RenderArgs args{.view_mat = camera_.get_view_mat(),
                          .camera_pos = camera_.pos,
                          .draw_imgui = imgui_enabled_};
    renderer_.render(args);
  }

  ResourceManager::shutdown();
  window_->shutdown();
  device_->shutdown();
}

void App::on_curse_pos_event(double xpos, double ypos) {
  const glm::vec2 pos = {xpos, ypos};
  if (first_mouse_) {
    first_mouse_ = false;
    last_pos_ = pos;
    return;
  }

  const glm::vec2 offset = {pos.x - last_pos_.x, last_pos_.y - pos.y};
  last_pos_ = pos;
  if (hide_mouse_) {
    camera_.process_mouse(offset);
  }
}

void App::on_key_event(int key, int action, [[maybe_unused]] int mods) {
  const auto is_press = action == GLFW_PRESS;
  if (is_press) {
    if (key == GLFW_KEY_ESCAPE) {
      hide_mouse_ = !hide_mouse_;
      on_hide_mouse_change();
    }
    if (key == GLFW_KEY_TAB) {
      window_->set_vsync(!window_->get_vsync());
    }
    if (key == GLFW_KEY_G && mods & GLFW_MOD_ALT) {
      imgui_enabled_ = !imgui_enabled_;
    }
    if (key == GLFW_KEY_C) {
      ResourceManager::get().free_model(models_[0]);
      models_.erase(models_.begin());
    }
  }
}

void App::on_hide_mouse_change() {
  glfwSetInputMode(window_->get_handle(), GLFW_CURSOR,
                   hide_mouse_ ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void App::load_config() {
  const std::filesystem::path config_file{resource_dir_ / "config.txt"};
  std::ifstream f(config_file);
  if (!f.is_open()) {
    LCRITICAL("Failed to load config file: {}", config_file.string());
    return;
  }

  f >> config_.initial_model_path;
}

void App::on_imgui() {
  ImGui::Begin("Hello world");
  renderer_.on_imgui();
  if (ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Position: %f %f %f", camera_.pos.x, camera_.pos.y, camera_.pos.z);
    ImGui::TreePop();
  }
  ImGui::End();
}

void App::load_model(const std::filesystem::path& path, const glm::mat4& transform) {
  models_.push_back(ResourceManager::get().load_model(path, transform));
}
