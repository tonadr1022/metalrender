#pragma once
#include <filesystem>
#include <span>

#include "Camera.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace teng {

namespace gfx {
class MemeRenderer123;
}

namespace rhi {
class Device;
class Swapchain;
}  // namespace rhi
class Window;

}  // namespace teng

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
  void load_model(const std::string &path, const glm::mat4 &transform = glm::mat4{1});
  void load_instances(const std::string &path, std::vector<glm::mat4> &&transforms);
  void init_camera();
  void write_camera();
  std::filesystem::path resolve_model_path(const std::string &path);

  void load_grid(glm::ivec3 radius, float dist, const std::string &path, float scale = 1.0f);
  void load_random_of_model(size_t count, float scale, float radius, const std::string &path);
  void load_scene_presets();
  void run_preset_scene(int idx);

  void load_grid(int radius, float dist, const std::string &path, float scale = 1.0f);
  void clear_all_models();
  struct ScenePreset {
    std::function<void()> load_fn;
    std::string name;
    Camera cam;
  };
  std::vector<ScenePreset> scene_presets_;

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
  std::unique_ptr<teng::rhi::Device> device_;
  teng::rhi::SwapchainHandleHolder swapchain_;
  std::unique_ptr<teng::Window> window_;
  std::unique_ptr<teng::gfx::MemeRenderer123> renderer_;
  static constexpr int k_camera_config_version{1};
  Camera camera_;
  std::vector<teng::ModelHandle> models_;
  bool first_mouse_{true};
  bool hide_mouse_{false};
  bool imgui_enabled_{true};

  glm::vec2 last_pos_{};
  // TODO: ptr for impl/RHI
  // std::unique_ptr<vox::Renderer> voxel_renderer_;
  // std::unique_ptr<vox::World> voxel_world_;
};
