#pragma once
#include <filesystem>

#include "Camera.hpp"
#include "gfx/RendererTypes.hpp"

namespace gfx {
class MemeRenderer123;
}
class Window;

struct App {
  App();
  App(const App &) = delete;
  App(App &&) = delete;
  App &operator=(const App &) = delete;
  App &operator=(App &&) = delete;
  ~App();

  void run();

 private:
  void on_curse_pos_event(double xpos, double ypos);
  void on_key_event(int key, int action, int mods);
  void shutdown();
  void on_hide_mouse_change();
  void load_config();
  void write_config();
  void on_imgui(float dt);
  void load_model(const std::string &path, const glm::mat4 &transform);
  void init_camera();
  void write_camera();

  struct Config {
    std::vector<std::filesystem::path> paths;
    glm::ivec2 win_dims{};
    glm::ivec2 win_pos{};
    glm::vec4 clear_color{0.1f, 0.1f, 0.1f, 1.0f};
    bool vsync{true};
  };
  Config config_;
  std::filesystem::path resource_dir_;
  std::filesystem::path shader_dir_;
  std::filesystem::path local_resource_dir_;
  std::filesystem::path camera_path_;
  std::filesystem::path config_path_;
  std::unique_ptr<rhi::Device> device_;
  std::unique_ptr<Window> window_;
  std::unique_ptr<gfx::MemeRenderer123> renderer_;
  static constexpr int k_camera_config_version{1};
  Camera camera_;
  std::vector<ModelHandle> models_;
  bool first_mouse_{true};
  bool hide_mouse_{false};
  bool imgui_enabled_{true};

  glm::vec2 last_pos_{};
  // TODO: ptr for impl/RHI
  // std::unique_ptr<vox::Renderer> voxel_renderer_;
  // std::unique_ptr<vox::World> voxel_world_;
};
