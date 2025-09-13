#pragma once
#include <filesystem>

#include "WindowApple.hpp"
#include "gfx/RendererMetal.hpp"
#include "gfx/metal/MetalDevice.hpp"

class Camera {
 public:
  [[nodiscard]] glm::mat4 get_view_mat() const;
  void calc_vectors();
  bool update_pos(GLFWwindow *window, float dt);
  bool process_mouse(glm::vec2 offset);

  glm::vec3 pos;
  float pitch{}, yaw{};
  glm::vec3 front{}, right{};
  glm::vec3 max_velocity{10.f};
  glm::vec3 velocity{};
  float acceleration_strength{100.0f};
  float damping{0.9f};
  float mouse_sensitivity{.1};
};

struct App {
  App();

  App(const App &) = delete;
  App(App &&) = delete;
  App &operator=(const App &) = delete;
  App &operator=(App &&) = delete;
  ~App() = default;
  void run();
  void on_key_event(double xpos, double ypos);

 private:
  std::filesystem::path resource_dir_;
  std::filesystem::path shader_dir_;
  std::unique_ptr<MetalDevice> device_;
  std::unique_ptr<WindowApple> window_;
  Camera camera_;
  bool first_mouse_{true};
  glm::vec2 last_pos_{};
  // TODO: ptr for impl/rhi
  RendererMetal renderer_;
};
