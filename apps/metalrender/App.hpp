#pragma once
#include <filesystem>
#include <span>

#include "../common/ScenePresets.hpp"
#include "Camera.hpp"
#include "FpsCameraController.hpp"
#include "core/Console.hpp"
#include "gfx/RendererTypes.hpp"
#include "gfx/rhi/GFXTypes.hpp"

namespace teng {

namespace gfx {
class MemeRenderer123;

namespace rhi {

class Device;
class Swapchain;

}  // namespace rhi

}  // namespace gfx

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
  void load_config();
  void write_config();
  void on_imgui(float dt);
  void load_model(const std::string &path, const glm::mat4 &transform = glm::mat4{1});
  void init_camera();
  void write_camera();

  void load_scene_presets();
  void run_preset_scene(int idx);

  void clear_all_models();
  void imgui_node(int node, teng::ModelInstance &model);
  std::vector<teng::demo_scenes::ScenePreset> scene_presets_;

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
  std::unique_ptr<teng::gfx::rhi::Device> device_;
  teng::gfx::rhi::SwapchainHandleHolder swapchain_;
  std::unique_ptr<teng::Window> window_;
  std::unique_ptr<teng::gfx::MemeRenderer123> renderer_;
  static constexpr int k_camera_config_version{1};
  FpsCameraController fps_camera_;
  std::vector<teng::ModelHandle> models_;
  bool imgui_enabled_{true};
  bool console_forced_imgui_{false};
  teng::Console console_;
  // TODO: ptr for impl/RHI
  // std::unique_ptr<vox::Renderer> voxel_renderer_;
  // std::unique_ptr<vox::World> voxel_world_;
};
